#include "CryptoFinanceSubsystem.h"

// Módulo que proporciona la implementación WebSocket de Unreal Engine.
#include "WebSocketsModule.h"

// FTSTicker: callbacks temporizados independientes del Tick de un Actor.
#include "Containers/Ticker.h"

// Permite medir tiempo transcurrido sin depender del reloj del sistema.
#include "HAL/PlatformTime.h"

// Deserialización de los mensajes JSON enviados por Binance.
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

// Transfiere callbacks recibidos por red al Game Thread.
#include "Async/Async.h"


// ============================================================
// CICLO DE VIDA
// ============================================================

/**
 * Initialize
 *
 * Prepara el catálogo, construye el filtro de símbolos y activa
 * los recursos necesarios para recibir y distribuir datos.
 */
void UCryptoFinanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    bIsShuttingDown = false;
    bNewDataReceived = false;

    ConnectionState = EConnectionState::Disconnected;
    ReconnectAttempt = 0;
    ConnectionGeneration = 0;

    LastMessageTimeSeconds = 0.0;
    ConnectionStartedAtSeconds = 0.0;

    // --------------------------------------------------------
    // 1. ASEGURAR QUE EL MÓDULO WEBSOCKETS ESTÉ DISPONIBLE
    // --------------------------------------------------------

    if (!FModuleManager::Get().IsModuleLoaded(TEXT("WebSockets")))
    {
        FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
    }

    // --------------------------------------------------------
    // 2. CARGAR EL CATÁLOGO PREDETERMINADO SI ESTÁ VACÍO
    // --------------------------------------------------------

    /*
     * TargetSymbols es la fuente de verdad del orden de presentación.
     * Si el editor ya proporcionó una lista, no la reemplazamos.
     */
    if (TargetSymbols.IsEmpty())
    {
        TargetSymbols =
        {
            TEXT("BTCUSDT"), TEXT("ETHUSDT"), TEXT("BNBUSDT"),
            TEXT("SOLUSDT"), TEXT("XRPUSDT"), TEXT("ADAUSDT"),
            TEXT("DOGEUSDT"), TEXT("AVAXUSDT"), TEXT("DOTUSDT"),
            TEXT("LINKUSDT"), TEXT("TRXUSDT"), TEXT("TONUSDT"),
            TEXT("SHIBUSDT"), TEXT("LTCUSDT"), TEXT("BCHUSDT"),
            TEXT("UNIUSDT"), TEXT("NEARUSDT"), TEXT("APTUSDT"),
            TEXT("ATOMUSDT"), TEXT("ETCUSDT"), TEXT("FILUSDT"),
            TEXT("ICPUSDT"), TEXT("ARBUSDT"), TEXT("OPUSDT"),
            TEXT("SUIUSDT")
        };
    }

    // --------------------------------------------------------
    // 3. NORMALIZAR Y ELIMINAR SÍMBOLOS DUPLICADOS
    // --------------------------------------------------------

    /*
     * Se conserva la primera aparición de cada símbolo.
     * TSet detecta duplicados y TArray conserva el orden original.
     */
    TSet<FString> UniqueSymbols;
    TArray<FString> NormalizedSymbols;

    NormalizedSymbols.Reserve(TargetSymbols.Num());
    UniqueSymbols.Reserve(TargetSymbols.Num());

    for (FString Symbol : TargetSymbols)
    {
        Symbol = Symbol.ToUpper().TrimStartAndEnd();

        if (Symbol.IsEmpty())
        {
            continue;
        }

        if (!UniqueSymbols.Contains(Symbol))
        {
            UniqueSymbols.Add(Symbol);
            NormalizedSymbols.Add(MoveTemp(Symbol));
        }
    }

    TargetSymbols = MoveTemp(NormalizedSymbols);

    // --------------------------------------------------------
    // 4. CONSTRUIR EL FILTRO DE PERTENENCIA UNA SOLA VEZ
    // --------------------------------------------------------

    /*
     * AllowedSymbols se consulta para cada activo recibido,
     * pero solo se construye aquí.
     */
    AllowedSymbols.Reset();
    AllowedSymbols.Reserve(TargetSymbols.Num());

    for (const FString& Symbol : TargetSymbols)
    {
        AllowedSymbols.Add(Symbol);
    }

    // --------------------------------------------------------
    // 5. VALIDAR EL INTERVALO DE DISTRIBUCIÓN
    // --------------------------------------------------------

    UpdateInterval = FMath::Clamp(UpdateInterval, 0.2f, 0.3f);

    // --------------------------------------------------------
    // 6. ACTIVAR LOS TICKERS Y LA CONEXIÓN
    // --------------------------------------------------------

    SetupTicker();
    SetupWatchdogTicker();
    ConnectWebSocket();

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[Vantage] Subsistema financiero inicializado.")
    );
}


/**
 * Deinitialize
 *
 * Invalida primero las operaciones pendientes. Después retira
 * los tickers y cierra el WebSocket.
 *
 * El orden es importante: los callbacks que lleguen durante el cierre
 * serán descartados mediante bIsShuttingDown y ConnectionGeneration.
 */
void UCryptoFinanceSubsystem::Deinitialize()
{
    bIsShuttingDown = true;
    ConnectionState = EConnectionState::ShuttingDown;

    // Invalida las tareas y callbacks asociados a conexiones anteriores.
    ++ConnectionGeneration;

    // --------------------------------------------------------
    // 1. RETIRAR EL TICKER DE DISTRIBUCIÓN
    // --------------------------------------------------------

    if (TickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
        TickerHandle.Reset();
    }

    // --------------------------------------------------------
    // 2. RETIRAR EL WATCHDOG
    // --------------------------------------------------------

    if (WatchdogTickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(WatchdogTickerHandle);
        WatchdogTickerHandle.Reset();
    }

    // --------------------------------------------------------
    // 3. CANCELAR CUALQUIER REINTENTO PROGRAMADO
    // --------------------------------------------------------

    if (ReconnectTickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(ReconnectTickerHandle);
        ReconnectTickerHandle.Reset();
    }

    // --------------------------------------------------------
    // 4. CERRAR Y LIBERAR EL WEBSOCKET
    // --------------------------------------------------------

    if (WebSocket.IsValid())
    {
        /*
         * Se conserva una referencia local durante Close().
         * Los callbacks provocados por este cierre serán descartados
         * porque el subsistema ya está en estado ShuttingDown.
         */
        TSharedPtr<IWebSocket> SocketToClose = WebSocket;
        WebSocket.Reset();

        if (SocketToClose->IsConnected())
        {
            SocketToClose->Close();
        }
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[Vantage] Subsistema financiero detenido.")
    );

    Super::Deinitialize();
}


// ============================================================
// CONEXIÓN WEBSOCKET Y RECONEXIÓN AUTOMÁTICA
// ============================================================

/**
 * ConnectWebSocket
 *
 * Crea una nueva instancia del cliente y registra sus callbacks.
 *
 * Cada conexión recibe un identificador incremental. Los callbacks
 * capturan ese identificador y solo pueden modificar el subsistema
 * si todavía coincide con ConnectionGeneration.
 *
 * Esta función debe ejecutarse en Game Thread.
 */
void UCryptoFinanceSubsystem::ConnectWebSocket()
{
    if (bIsShuttingDown)
    {
        return;
    }

    // Evita crear otra conexión si ya existe una en curso o activa.
    if (ConnectionState == EConnectionState::Connecting ||
        ConnectionState == EConnectionState::Connected)
    {
        return;
    }

    // Si quedara un reintento pendiente, se cancela antes de conectar.
    if (ReconnectTickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(ReconnectTickerHandle);
        ReconnectTickerHandle.Reset();
    }

    const FString ServerUrl =
        TEXT("wss://stream.binance.com:9443/ws/!miniTicker@arr");

    // Cada intento crea una nueva generación de conexión.
    const uint64 ThisGeneration = ++ConnectionGeneration;

    ConnectionState = EConnectionState::Connecting;

    // El watchdog utilizará este instante para detectar handshakes bloqueados.
    ConnectionStartedAtSeconds = FPlatformTime::Seconds();

    // Todavía no se recibió ningún mensaje para esta generación.
    LastMessageTimeSeconds = 0.0;

    WebSocket = FWebSocketsModule::Get().CreateWebSocket(ServerUrl);

    if (!WebSocket.IsValid())
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[Vantage] No se pudo crear el cliente WebSocket.")
        );

        HandleConnectionFailure(
            ThisGeneration,
            TEXT("No se pudo crear el cliente WebSocket.")
        );

        return;
    }

    const TWeakObjectPtr<UCryptoFinanceSubsystem> WeakThis(this);

    // --------------------------------------------------------
    // CALLBACK: CONEXIÓN EXITOSA
    // --------------------------------------------------------

    WebSocket->OnConnected().AddLambda(
        [WeakThis, ThisGeneration]()
        {
            /*
             * Los callbacks de red no modifican directamente el UObject.
             * La confirmación del estado se realiza en Game Thread.
             */
            AsyncTask(
                ENamedThreads::GameThread,
                [WeakThis, ThisGeneration]()
                {
                    if (!WeakThis.IsValid())
                    {
                        return;
                    }

                    UCryptoFinanceSubsystem* Subsystem = WeakThis.Get();

                    if (!Subsystem || Subsystem->bIsShuttingDown)
                    {
                        return;
                    }

                    Subsystem->HandleSocketConnected(ThisGeneration);
                }
            );
        }
    );

    // --------------------------------------------------------
    // CALLBACK: ERROR DE CONEXIÓN
    // --------------------------------------------------------

    WebSocket->OnConnectionError().AddLambda(
        [WeakThis, ThisGeneration](const FString& Error)
        {
            AsyncTask(
                ENamedThreads::GameThread,
                [WeakThis, ThisGeneration, Error]()
                {
                    if (!WeakThis.IsValid())
                    {
                        return;
                    }

                    UCryptoFinanceSubsystem* Subsystem = WeakThis.Get();

                    if (!Subsystem || Subsystem->bIsShuttingDown)
                    {
                        return;
                    }

                    Subsystem->HandleConnectionFailure(
                        ThisGeneration,
                        FString::Printf(
                            TEXT("Error de conexión: %s"),
                            *Error
                        )
                    );
                }
            );
        }
    );

    // --------------------------------------------------------
    // CALLBACK: CONEXIÓN CERRADA
    // --------------------------------------------------------

    WebSocket->OnClosed().AddLambda(
        [WeakThis, ThisGeneration](
            int32 StatusCode,
            const FString& Reason,
            bool bWasClean)
        {
            const FString CloseDescription = FString::Printf(
                TEXT("WebSocket cerrado. Código: %d. Razón: %s. Cierre limpio: %s"),
                StatusCode,
                *Reason,
                bWasClean ? TEXT("Sí") : TEXT("No")
            );

            AsyncTask(
                ENamedThreads::GameThread,
                [WeakThis, ThisGeneration, CloseDescription]()
                {
                    if (!WeakThis.IsValid())
                    {
                        return;
                    }

                    UCryptoFinanceSubsystem* Subsystem = WeakThis.Get();

                    if (!Subsystem || Subsystem->bIsShuttingDown)
                    {
                        return;
                    }

                    Subsystem->HandleConnectionFailure(
                        ThisGeneration,
                        CloseDescription
                    );
                }
            );
        }
    );

    // --------------------------------------------------------
    // CALLBACK: RECEPCIÓN DE MENSAJES
    // --------------------------------------------------------

    /*
     * El callback puede ejecutarse fuera del Game Thread.
     * Copiamos el mensaje y lo procesamos en el hilo principal.
     *
     * El momento de recepción también se registra en Game Thread.
     * Ese dato es utilizado por el watchdog para detectar inactividad.
     */
    WebSocket->OnMessage().AddLambda(
        [WeakThis, ThisGeneration](const FString& MessageString)
        {
            AsyncTask(
                ENamedThreads::GameThread,
                [WeakThis, ThisGeneration, MessageString]()
                {
                    if (!WeakThis.IsValid())
                    {
                        return;
                    }

                    UCryptoFinanceSubsystem* Subsystem = WeakThis.Get();

                    if (!Subsystem ||
                        Subsystem->bIsShuttingDown ||
                        !Subsystem->IsCurrentConnection(ThisGeneration) ||
                        Subsystem->ConnectionState != EConnectionState::Connected)
                    {
                        return;
                    }

                    // Actualiza el instante de recepción para el watchdog.
                    Subsystem->LastMessageTimeSeconds = FPlatformTime::Seconds();

                    // El procesamiento JSON también ocurre en Game Thread.
                    Subsystem->ProcessWebSocketMessage(MessageString);
                }
            );
        }
    );

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[Vantage] Iniciando conexión WebSocket. Generación: %llu"),
        ThisGeneration
    );

    // Los callbacks quedan registrados antes de iniciar la conexión.
    WebSocket->Connect();
}


/**
 * HandleSocketConnected
 *
 * Confirma que la conexión correspondiente a la generación indicada
 * sigue siendo la conexión vigente.
 *
 * Reinicia el contador de reintentos y el reloj de recepción.
 */
void UCryptoFinanceSubsystem::HandleSocketConnected(uint64 ConnectedGeneration)
{
    if (bIsShuttingDown || !IsCurrentConnection(ConnectedGeneration))
    {
        return;
    }

    ConnectionState = EConnectionState::Connected;
    ReconnectAttempt = 0;

    const double CurrentTime = FPlatformTime::Seconds();

    // El timeout de mensajes comienza a contar desde la conexión exitosa.
    LastMessageTimeSeconds = CurrentTime;
    ConnectionStartedAtSeconds = CurrentTime;

    if (ReconnectTickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(ReconnectTickerHandle);
        ReconnectTickerHandle.Reset();
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[Vantage] Conectado a Binance. Generación: %llu"),
        ConnectedGeneration
    );
}


/**
 * HandleConnectionFailure
 *
 * Centraliza el tratamiento de errores, cierres inesperados
 * y desconexiones detectadas por el watchdog.
 *
 * Primero verifica que el evento pertenezca al socket vigente.
 * Después invalida esa generación, libera el socket y programa un reintento.
 *
 * Si OnConnectionError y OnClosed llegan casi juntos, el segundo evento
 * será descartado por la validación de generación.
 */
void UCryptoFinanceSubsystem::HandleConnectionFailure(
    uint64 FailedGeneration,
    const FString& Reason)
{
    if (bIsShuttingDown || !IsCurrentConnection(FailedGeneration))
    {
        return;
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[Vantage] Conexión perdida. %s"),
        *Reason
    );

    // Invalida inmediatamente los callbacks del socket anterior.
    ++ConnectionGeneration;

    ConnectionState = EConnectionState::Disconnected;

    // Reinicia los relojes asociados a la conexión que acaba de fallar.
    LastMessageTimeSeconds = 0.0;
    ConnectionStartedAtSeconds = 0.0;

    if (WebSocket.IsValid())
    {
        /*
         * Se conserva una referencia local durante Close().
         * Si el cierre dispara OnClosed, su generación ya no será válida.
         */
        TSharedPtr<IWebSocket> FailedSocket = WebSocket;
        WebSocket.Reset();

        if (FailedSocket->IsConnected())
        {
            FailedSocket->Close();
        }
    }

    ScheduleReconnect();
}


/**
 * ScheduleReconnect
 *
 * Programa un solo reintento mediante CoreTicker.
 *
 * Backoff exponencial:
 * 1 s -> 2 s -> 4 s -> 8 s -> 16 s -> 30 s -> 30 s...
 *
 * No existe un límite de intentos. El intervalo sí tiene un máximo.
 */
void UCryptoFinanceSubsystem::ScheduleReconnect()
{
    if (bIsShuttingDown ||
        ConnectionState == EConnectionState::ShuttingDown ||
        ReconnectTickerHandle.IsValid())
    {
        return;
    }

    const float ExponentialDelay =
        ReconnectBaseDelay * FMath::Pow(
            2.0f,
            static_cast<float>(ReconnectAttempt)
        );

    const float Delay = FMath::Min(
        ExponentialDelay,
        ReconnectMaxDelay
    );

    ++ReconnectAttempt;
    ConnectionState = EConnectionState::Reconnecting;

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[Vantage] Próximo intento de reconexión en %.1f segundos. Intento: %d"),
        Delay,
        ReconnectAttempt
    );

    ReconnectTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(
            this,
            &UCryptoFinanceSubsystem::HandleReconnectTimer
        ),
        Delay
    );
}


/**
 * HandleReconnectTimer
 *
 * Callback de una sola ejecución. Limpia su propio handle e inicia
 * un nuevo intento de conexión.
 *
 * Devuelve false para que FTSTicker no lo repita.
 */
bool UCryptoFinanceSubsystem::HandleReconnectTimer(float DeltaTime)
{
    ReconnectTickerHandle.Reset();

    if (bIsShuttingDown ||
        ConnectionState == EConnectionState::ShuttingDown)
    {
        return false;
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[Vantage] Ejecutando intento de reconexión %d."),
        ReconnectAttempt
    );

    ConnectWebSocket();

    return false;
}


/**
 * IsCurrentConnection
 *
 * Verifica si un callback todavía pertenece al socket vigente.
 * La generación solo se modifica en Game Thread.
 */
bool UCryptoFinanceSubsystem::IsCurrentConnection(uint64 CandidateGeneration) const
{
    return CandidateGeneration == ConnectionGeneration;
}


// ============================================================
// WATCHDOG: SUPERVISIÓN DE LA CONEXIÓN
// ============================================================

/**
 * SetupWatchdogTicker
 *
 * Registra el callback periódico que supervisa la actividad del WebSocket.
 *
 * El watchdog no procesa mensajes ni modifica datos financieros.
 * Su única responsabilidad es detectar conexiones que dejaron de responder.
 */
void UCryptoFinanceSubsystem::SetupWatchdogTicker()
{
    if (WatchdogTickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(WatchdogTickerHandle);
        WatchdogTickerHandle.Reset();
    }

    WatchdogTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(
            this,
            &UCryptoFinanceSubsystem::HandleWatchdogTicker
        ),
        WatchdogCheckInterval
    );

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[Vantage] Watchdog activado. Intervalo: %.1f s. Timeout: %.1f s."),
        WatchdogCheckInterval,
        static_cast<float>(MessageTimeoutSeconds)
    );
}


/**
 * HandleWatchdogTicker
 *
 * Supervisa dos situaciones:
 *
 * 1. Connecting:
 *    El handshake no se completó dentro del tiempo permitido.
 *
 * 2. Connected:
 *    El socket dejó de entregar mensajes durante demasiado tiempo.
 *
 * Cuando detecta una de estas situaciones, utiliza HandleConnectionFailure().
 * De esta forma se reutilizan la invalidación de generaciones y el backoff.
 *
 * @return true para mantener activo el ticker.
 */
bool UCryptoFinanceSubsystem::HandleWatchdogTicker(float DeltaTime)
{
    if (bIsShuttingDown)
    {
        return false;
    }

    const double CurrentTime = FPlatformTime::Seconds();

    // --------------------------------------------------------
    // 1. DETECTAR HANDSHAKES QUE NO TERMINAN
    // --------------------------------------------------------

    if (ConnectionState == EConnectionState::Connecting)
    {
        const double ConnectionElapsed =
            CurrentTime - ConnectionStartedAtSeconds;

        if (ConnectionStartedAtSeconds > 0.0 &&
            ConnectionElapsed >= ConnectionTimeoutSeconds)
        {
            const uint64 TimedOutGeneration = ConnectionGeneration;

            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[Vantage] Timeout durante el establecimiento de la conexión. Transcurrido: %.1f s."),
                ConnectionElapsed
            );

            HandleConnectionFailure(
                TimedOutGeneration,
                TEXT("Timeout: el handshake no se completó dentro del tiempo permitido.")
            );
        }

        return true;
    }

    // --------------------------------------------------------
    // 2. DETECTAR CONEXIONES ACTIVAS SIN MENSAJES
    // --------------------------------------------------------

    if (ConnectionState == EConnectionState::Connected)
    {
        const double InactiveTime =
            CurrentTime - LastMessageTimeSeconds;

        if (LastMessageTimeSeconds > 0.0 &&
            InactiveTime >= MessageTimeoutSeconds)
        {
            const uint64 InactiveGeneration = ConnectionGeneration;

            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[Vantage] Watchdog detectó inactividad. Último mensaje hace %.1f s."),
                InactiveTime
            );

            HandleConnectionFailure(
                InactiveGeneration,
                FString::Printf(
                    TEXT("Watchdog: no se recibieron mensajes durante %.1f segundos."),
                    InactiveTime
                )
            );
        }
    }

    return true;
}


// ============================================================
// PROCESAMIENTO JSON
// ============================================================

/**
 * ProcessWebSocketMessage
 *
 * Deserializa un arreglo miniTicker y actualiza solamente los símbolos
 * configurados. Se ejecuta en Game Thread.
 *
 * Mutex protege el caché y la bandera de actualización frente
 * a futuras rutas de acceso concurrentes.
 */
void UCryptoFinanceSubsystem::ProcessWebSocketMessage(const FString& MessageString)
{
    // --------------------------------------------------------
    // 1. DESERIALIZAR EL ARREGLO JSON
    // --------------------------------------------------------

    TArray<TSharedPtr<FJsonValue>> JsonArray;

    const TSharedRef<TJsonReader<>> Reader =
        TJsonReaderFactory<>::Create(MessageString);

    if (!FJsonSerializer::Deserialize(Reader, JsonArray))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[Vantage] No se pudo deserializar un mensaje de Binance.")
        );

        return;
    }

    bool bUpdatedAny = false;

    // --------------------------------------------------------
    // 2. ACTUALIZAR EL CACHÉ BAJO BLOQUEO
    // --------------------------------------------------------

    {
        FScopeLock Lock(&Mutex);

        for (const TSharedPtr<FJsonValue>& Value : JsonArray)
        {
            if (!Value.IsValid())
            {
                continue;
            }

            const TSharedPtr<FJsonObject> JsonObject = Value->AsObject();

            if (!JsonObject.IsValid())
            {
                continue;
            }

            // ------------------------------------------------
            // 2.1. EXTRAER Y NORMALIZAR EL SÍMBOLO
            // ------------------------------------------------

            FString Symbol;

            if (!JsonObject->TryGetStringField(TEXT("s"), Symbol))
            {
                continue;
            }

            Symbol = Symbol.ToUpper().TrimStartAndEnd();

            if (Symbol.IsEmpty())
            {
                continue;
            }

            // ------------------------------------------------
            // 2.2. FILTRAR ANTES DE LEER EL RESTO DE LOS CAMPOS
            // ------------------------------------------------

            /*
             * AllowedSymbols se creó durante Initialize().
             * No se reconstruye durante el procesamiento normal.
             */
            if (!AllowedSymbols.Contains(Symbol))
            {
                continue;
            }

            // ------------------------------------------------
            // 2.3. EXTRAER LOS CAMPOS DEL MINI TICKER
            // ------------------------------------------------

            FString LastPriceString;
            FString OpenPriceString;
            FString HighPriceString;
            FString LowPriceString;
            FString VolumeString;

            const bool bHasLastPrice =
                JsonObject->TryGetStringField(TEXT("c"), LastPriceString);

            const bool bHasOpenPrice =
                JsonObject->TryGetStringField(TEXT("o"), OpenPriceString);

            const bool bHasHighPrice =
                JsonObject->TryGetStringField(TEXT("h"), HighPriceString);

            const bool bHasLowPrice =
                JsonObject->TryGetStringField(TEXT("l"), LowPriceString);

            const bool bHasVolume =
                JsonObject->TryGetStringField(TEXT("v"), VolumeString);

            // No almacenamos registros incompletos.
            if (!bHasLastPrice ||
                !bHasOpenPrice ||
                !bHasHighPrice ||
                !bHasLowPrice ||
                !bHasVolume)
            {
                continue;
            }

            // ------------------------------------------------
            // 2.4. CONVERTIR LOS VALORES A DOUBLE
            // ------------------------------------------------

            const double LastPrice = FCString::Atod(*LastPriceString);
            const double OpenPrice = FCString::Atod(*OpenPriceString);
            const double HighPrice = FCString::Atod(*HighPriceString);
            const double LowPrice = FCString::Atod(*LowPriceString);
            const double Volume = FCString::Atod(*VolumeString);

            // ------------------------------------------------
            // 2.5. CONSTRUIR EL REGISTRO DEL ACTIVO
            // ------------------------------------------------

            FCryptoAssetData AssetData;

            AssetData.Symbol = Symbol;
            AssetData.LastPrice = LastPrice;
            AssetData.HighPrice = HighPrice;
            AssetData.LowPrice = LowPrice;
            AssetData.Volume = Volume;

            // ------------------------------------------------
            // 2.6. CALCULAR LAS VARIACIONES
            // ------------------------------------------------

            AssetData.PriceChange = LastPrice - OpenPrice;

            AssetData.PriceChangePercent = (OpenPrice > 0.0)
                ? ((LastPrice - OpenPrice) / OpenPrice) * 100.0
                : 0.0;

            // ------------------------------------------------
            // 2.7. INSERTAR O REEMPLAZAR EN EL TMAP
            // ------------------------------------------------

            /*
             * Add agrega el elemento si la clave no existe o reemplaza
             * el valor anterior si el símbolo ya estaba almacenado.
             */
            AssetCache.Add(Symbol, MoveTemp(AssetData));
            bUpdatedAny = true;
        }

        if (bUpdatedAny)
        {
            bNewDataReceived = true;
        }
    }
}


// ============================================================
// TICKER Y DISTRIBUCIÓN
// ============================================================

/**
 * SetupTicker
 *
 * Registra HandleTicker en CoreTicker. Antes elimina un registro anterior
 * para impedir que se acumulen callbacks duplicados.
 */
void UCryptoFinanceSubsystem::SetupTicker()
{
    if (TickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
        TickerHandle.Reset();
    }

    const float ClampedInterval = FMath::Clamp(UpdateInterval, 0.2f, 0.3f);

    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(
            this,
            &UCryptoFinanceSubsystem::HandleTicker
        ),
        ClampedInterval
    );
}


/**
 * HandleTicker
 *
 * Comprueba si llegaron datos nuevos. Cuando corresponde, construye
 * una instantánea ordenada y emite el delegado fuera del bloqueo.
 */
bool UCryptoFinanceSubsystem::HandleTicker(float DeltaTime)
{
    if (bIsShuttingDown)
    {
        return false;
    }

    bool bShouldBroadcast = false;
    TArray<FCryptoAssetData> SnapshotList;

    {
        FScopeLock Lock(&Mutex);

        if (bNewDataReceived)
        {
            bNewDataReceived = false;
            bShouldBroadcast = true;

            /*
             * Se recorre TargetSymbols para entregar un orden determinista.
             * No se agregan placeholders al evento: solo se distribuyen
             * activos para los que ya se recibió información real.
             */
            BuildOrderedSnapshot(SnapshotList, false);
        }
    }

    // Nunca ejecutamos código externo mientras Mutex está tomado.
    if (bShouldBroadcast)
    {
        OnCryptoDataUpdated.Broadcast(SnapshotList);
    }

    return true;
}


// ============================================================
// CONSTRUCCIÓN DE SNAPSHOTS ORDENADOS
// ============================================================

/**
 * BuildOrderedSnapshot
 *
 * Recorre el catálogo y busca cada activo en AssetCache.
 * El llamador debe adquirir Mutex antes de invocar esta función.
 *
 * No se crea una segunda colección permanente: el array de salida
 * es una instantánea temporal para una consulta o emisión del delegado.
 */
void UCryptoFinanceSubsystem::BuildOrderedSnapshot(
    TArray<FCryptoAssetData>& OutAssets,
    bool bIncludePlaceholders
) const
{
    OutAssets.Reset();
    OutAssets.Reserve(TargetSymbols.Num());

    for (const FString& Symbol : TargetSymbols)
    {
        if (Symbol.IsEmpty())
        {
            continue;
        }

        if (const FCryptoAssetData* Found = AssetCache.Find(Symbol))
        {
            OutAssets.Add(*Found);
        }
        else if (bIncludePlaceholders)
        {
            FCryptoAssetData Placeholder;
            Placeholder.Symbol = Symbol;
            OutAssets.Add(MoveTemp(Placeholder));
        }
    }
}


// ============================================================
// CONSULTAS PÚBLICAS
// ============================================================

/**
 * GetAssetData
 *
 * Busca un activo por símbolo y devuelve una copia independiente.
 * No se expone un puntero interno al TMap para evitar referencias
 * que podrían invalidarse cuando el mapa se modifique.
 */
bool UCryptoFinanceSubsystem::GetAssetData(
    const FString& Symbol,
    FCryptoAssetData& OutData
)
{
    const FString NormalizedSymbol = Symbol.ToUpper().TrimStartAndEnd();

    if (NormalizedSymbol.IsEmpty())
    {
        return false;
    }

    FScopeLock Lock(&Mutex);

    if (const FCryptoAssetData* Found = AssetCache.Find(NormalizedSymbol))
    {
        OutData = *Found;
        return true;
    }

    return false;
}


/**
 * GetAllAssets
 *
 * Devuelve los activos que ya recibieron datos, respetando TargetSymbols.
 * No depende del orden interno del TMap.
 */
void UCryptoFinanceSubsystem::GetAllAssets(TArray<FCryptoAssetData>& OutAssets)
{
    FScopeLock Lock(&Mutex);
    BuildOrderedSnapshot(OutAssets, false);
}


/**
 * GetOrderedTargetAssets
 *
 * Devuelve una entrada por cada símbolo configurado.
 * Los activos que todavía no recibieron mensajes se representan
 * mediante placeholders.
 *
 * Este método es el indicado para terminales que necesitan posiciones
 * fijas y paginación estable desde el primer momento.
 */
void UCryptoFinanceSubsystem::GetOrderedTargetAssets(
    TArray<FCryptoAssetData>& OutOrderedAssets
)
{
    FScopeLock Lock(&Mutex);
    BuildOrderedSnapshot(OutOrderedAssets, true);
}