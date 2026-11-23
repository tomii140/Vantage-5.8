#include "CryptoFinanceSubsystem.h"

// Módulo que proporciona la implementación WebSocket de Unreal Engine.
#include "WebSocketsModule.h"

// FTSTicker: mecanismo de callbacks periódicos independiente del Tick de un Actor.
#include "Containers/Ticker.h"

// Deserialización de los mensajes JSON enviados por Binance.
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

// Permite transferir el procesamiento recibido por red al Game Thread.
#include "Async/Async.h"


// ============================================================
// CICLO DE VIDA
// ============================================================

/**
 * Initialize
 *
 * Prepara el catálogo, construye el filtro de símbolos y activa los recursos
 * que necesita el subsistema para recibir y distribuir datos.
 */
void UCryptoFinanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    bIsShuttingDown = false;
    bNewDataReceived = false;

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
     * Se conserva la primera aparición de cada símbolo. TSet se usa
     * temporalmente para detectar duplicados, mientras que el TArray
     * resultante conserva el orden original del catálogo.
     */
    TSet<FString> UniqueSymbols;
    TArray<FString> NormalizedSymbols;
    NormalizedSymbols.Reserve(TargetSymbols.Num());
    UniqueSymbols.Reserve(TargetSymbols.Num());

    for (FString Symbol : TargetSymbols)
    {
        Symbol = Symbol.ToUpper().TrimStartAndEnd();

        // Los identificadores vacíos no representan mercados válidos.
        if (Symbol.IsEmpty())
        {
            continue;
        }

        // Contains permite conservar la primera aparición sin alterar el orden.
        if (!UniqueSymbols.Contains(Symbol))
        {
            UniqueSymbols.Add(Symbol);
            NormalizedSymbols.Add(MoveTemp(Symbol));
        }
    }

    // Reemplazamos el catálogo por su versión normalizada y sin duplicados.
    TargetSymbols = MoveTemp(NormalizedSymbols);

    // --------------------------------------------------------
    // 4. CONSTRUIR EL FILTRO DE PERTENENCIA UNA SOLA VEZ
    // --------------------------------------------------------

    /*
     * AllowedSymbols se consulta para cada activo recibido, pero solo se
     * construye aquí. Evitamos crear o reconstruir un TSet en cada mensaje.
     *
     * Si el catálogo queda vacío, el filtro no aceptará ningún símbolo:
     * esto evita interpretar accidentalmente una lista vacía como "todos".
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
    // 6. ACTIVAR CONEXIÓN Y TICKER
    // --------------------------------------------------------

    ConnectWebSocket();
    SetupTicker();
}


/**
 * Deinitialize
 *
 * Detiene los callbacks periódicos y solicita el cierre del WebSocket.
 * La bandera de cierre se activa antes de liberar recursos para que cualquier
 * tarea pendiente en el Game Thread descarte el mensaje recibido.
 */
void UCryptoFinanceSubsystem::Deinitialize()
{
    // Desde este punto no aceptamos más procesamiento de mensajes.
    bIsShuttingDown = true;

    // --------------------------------------------------------
    // 1. RETIRAR EL TICKER
    // --------------------------------------------------------

    if (TickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
        TickerHandle.Reset();
    }

    // --------------------------------------------------------
    // 2. CERRAR Y LIBERAR EL WEBSOCKET
    // --------------------------------------------------------

    if (WebSocket.IsValid())
    {
        if (WebSocket->IsConnected())
        {
            WebSocket->Close();
        }

        WebSocket.Reset();
    }

    Super::Deinitialize();
}


// ============================================================
// CONEXIÓN WEBSOCKET
// ============================================================

/**
 * ConnectWebSocket
 *
 * Crea el cliente y registra los callbacks de conexión.
 * El callback OnMessage no consulta miembros ni procesa JSON desde el hilo
 * de red: solamente copia el mensaje y agenda el trabajo en Game Thread.
 */
void UCryptoFinanceSubsystem::ConnectWebSocket()
{
    const FString ServerUrl =
        TEXT("wss://stream.binance.com:9443/ws/!miniTicker@arr");

    WebSocket = FWebSocketsModule::Get().CreateWebSocket(ServerUrl);

    if (!WebSocket.IsValid())
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[Vantage] No se pudo crear el cliente WebSocket.")
        );
        return;
    }

    // --------------------------------------------------------
    // CALLBACK: CONEXIÓN EXITOSA
    // --------------------------------------------------------

    WebSocket->OnConnected().AddLambda([]()
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[Vantage] Conectado al WebSocket de Binance (!miniTicker@arr).")
        );
    });

    // --------------------------------------------------------
    // CALLBACK: ERROR DE CONEXIÓN
    // --------------------------------------------------------

    WebSocket->OnConnectionError().AddLambda([](const FString& Error)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[Vantage] Error de conexión WebSocket: %s"),
            *Error
        );
    });

    // --------------------------------------------------------
    // CALLBACK: CONEXIÓN CERRADA
    // --------------------------------------------------------

    WebSocket->OnClosed().AddLambda(
        [](int32 StatusCode, const FString& Reason, bool bWasClean)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[Vantage] WebSocket cerrado. Código: %d. Razón: %s. Cierre limpio: %s"),
                StatusCode,
                *Reason,
                bWasClean ? TEXT("Sí") : TEXT("No")
            );
        }
    );

    // --------------------------------------------------------
    // CALLBACK: RECEPCIÓN DE MENSAJES
    // --------------------------------------------------------

    /*
     * El callback de red puede ejecutarse fuera del Game Thread.
     * Capturamos una referencia débil para no prolongar la vida del UObject
     * ni acceder a sus miembros desde el hilo de recepción.
     */
    const TWeakObjectPtr<UCryptoFinanceSubsystem> WeakThis(this);

    WebSocket->OnMessage().AddLambda(
        [WeakThis](const FString& MessageString)
        {
            // La cadena se copia a la tarea que se ejecutará en Game Thread.
            AsyncTask(
                ENamedThreads::GameThread,
                [WeakThis, MessageString]()
                {
                    // El subsistema pudo destruirse mientras la tarea esperaba.
                    if (!WeakThis.IsValid())
                    {
                        return;
                    }

                    UCryptoFinanceSubsystem* Subsystem = WeakThis.Get();

                    // También descartamos tareas si el cierre ya comenzó.
                    if (!Subsystem || Subsystem->bIsShuttingDown)
                    {
                        return;
                    }

                    Subsystem->ProcessWebSocketMessage(MessageString);
                }
            );
        }
    );

    // Los callbacks quedan registrados antes de iniciar la conexión.
    WebSocket->Connect();
}


// ============================================================
// PROCESAMIENTO JSON
// ============================================================

/**
 * ProcessWebSocketMessage
 *
 * Deserializa un arreglo miniTicker y actualiza solamente los símbolos
 * configurados. Se ejecuta en Game Thread, pero Mutex se conserva para
 * proteger el caché ante futuros consumidores o rutas de acceso concurrentes.
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
             * AllowedSymbols se creó durante Initialize() y es de solo
             * lectura durante el procesamiento normal. Si está vacío,
             * ningún símbolo se considera permitido.
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
             * El TMap continúa siendo la única fuente de datos de mercado.
             */
            AssetCache.Add(Symbol, MoveTemp(AssetData));
            bUpdatedAny = true;
        }

        // La bandera se activa solamente si hubo al menos una actualización.
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
 * Registra HandleTicker en el CoreTicker. Antes elimina un registro anterior
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
 * Comprueba si llegaron datos nuevos. Cuando corresponde, construye una
 * instantánea ordenada y emite el delegado fuera del bloqueo.
 *
 * La instantánea es necesaria porque el delegado recibe un array y los
 * consumidores deben observar una colección consistente durante Broadcast().
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
             * Construimos el snapshot recorriendo TargetSymbols. Así el evento
             * entrega un orden determinista y no depende del orden interno del
             * TMap. No agregamos placeholders al evento: solo se distribuyen
             * activos para los que ya se recibió información real.
             */
            BuildOrderedSnapshot(SnapshotList, false);
        }
    }

    // Nunca ejecutamos código externo de los suscriptores mientras Mutex está tomado.
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
 * Recorre el catálogo y busca cada activo en AssetCache. El llamador debe
 * adquirir Mutex antes de invocar esta función.
 *
 * No se crea una segunda colección permanente: el array de salida es una
 * instantánea temporal para una consulta o una emisión del delegado.
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
            // Copiamos el registro al snapshot de salida.
            OutAssets.Add(*Found);
        }
        else if (bIncludePlaceholders)
        {
            /*
             * La posición se conserva aunque todavía no haya llegado una
             * cotización. Los valores numéricos permanecen inicializados en cero.
             */
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
 * Busca un activo por símbolo y devuelve una copia independiente de su registro.
 * No se expone un puntero interno al TMap para evitar que el consumidor conserve
 * una referencia que podría invalidarse cuando el mapa se modifique.
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
 * A diferencia de GenerateValueArray(), esta implementación no depende del
 * orden interno del TMap.
 */
void UCryptoFinanceSubsystem::GetAllAssets(TArray<FCryptoAssetData>& OutAssets)
{
    FScopeLock Lock(&Mutex);
    BuildOrderedSnapshot(OutAssets, false);
}


/**
 * GetOrderedTargetAssets
 *
 * Devuelve una entrada por cada símbolo configurado. Los activos que todavía
 * no recibieron mensajes se representan mediante placeholders.
 *
 * Este método es el indicado para terminales que necesitan posiciones fijas
 * y paginación estable desde el primer momento.
 */
void UCryptoFinanceSubsystem::GetOrderedTargetAssets(
    TArray<FCryptoAssetData>& OutOrderedAssets
)
{
    FScopeLock Lock(&Mutex);
    BuildOrderedSnapshot(OutOrderedAssets, true);
}
