
#include "CryptoFinanceSubsystem.h"

// Módulo responsable de proporcionar la implementación WebSocket de Unreal Engine.
#include "WebSocketsModule.h"

// Proporciona FTSTicker, utilizado para ejecutar actualizaciones periódicas.
#include "Containers/Ticker.h"

// Herramientas de serialización y lectura de JSON.
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"


/**
 * Initialize
 *
 * Inicializa el sistema financiero de Vantage.
 *
 * Esta función es ejecutada por Unreal Engine cuando se crea
 * el subsistema asociado al GameInstance.
 *
 * En esta etapa preparamos todos los recursos necesarios
 * antes de comenzar a recibir información del mercado.
 */
void UCryptoFinanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    // Ejecuta primero la inicialización correspondiente a UGameInstanceSubsystem.
    Super::Initialize(Collection);


    // ------------------------------------------------------------
    // 1. CARGA DEL MÓDULO WEBSOCKETS
    // ------------------------------------------------------------

    /*
     * Comprueba si el módulo WebSockets ya se encuentra cargado.
     *
     * Si todavía no está cargado, lo solicita al administrador
     * de módulos de Unreal Engine.
     *
     * LoadModuleChecked garantiza que el módulo esté disponible
     * y genera un error si no puede cargarse.
     */
    if (!FModuleManager::Get().IsModuleLoaded(TEXT("WebSockets")))
    {
        FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
    }


    // ------------------------------------------------------------
    // 2. CONFIGURACIÓN DEL CATÁLOGO DE ACTIVOS
    // ------------------------------------------------------------

    /*
     * Si no se configuraron símbolos desde el editor,
     * cargamos una lista inicial de 25 pares de negociación USDT.
     *
     * Si TargetSymbols ya contiene elementos, respetamos
     * la configuración existente.
     *
     * Esta lista representa los activos que Vantage desea visualizar,
     * no todos los activos disponibles en Binance.
     */
    if (TargetSymbols.Num() == 0)
    {
        TargetSymbols =
        {
            TEXT("BTCUSDT"),
            TEXT("ETHUSDT"),
            TEXT("BNBUSDT"),
            TEXT("SOLUSDT"),
            TEXT("XRPUSDT"),

            TEXT("ADAUSDT"),
            TEXT("DOGEUSDT"),
            TEXT("AVAXUSDT"),
            TEXT("DOTUSDT"),
            TEXT("LINKUSDT"),

            TEXT("TRXUSDT"),
            TEXT("TONUSDT"),
            TEXT("SHIBUSDT"),
            TEXT("LTCUSDT"),
            TEXT("BCHUSDT"),

            TEXT("UNIUSDT"),
            TEXT("NEARUSDT"),
            TEXT("APTUSDT"),
            TEXT("ATOMUSDT"),
            TEXT("ETCUSDT"),

            TEXT("FILUSDT"),
            TEXT("ICPUSDT"),
            TEXT("ARBUSDT"),
            TEXT("OPUSDT"),
            TEXT("SUIUSDT")
        };
    }


    // ------------------------------------------------------------
    // 3. NORMALIZACIÓN DE SÍMBOLOS
    // ------------------------------------------------------------

    /*
     * Normaliza todos los símbolos para mantener un formato uniforme.
     *
     * ToUpper():
     * Convierte el texto a mayúsculas.
     *
     * TrimStartAndEnd():
     * Elimina espacios al principio y al final.
     *
     * Esto evita que BTCUSDT, btcusdt y " BTCUSDT " sean
     * interpretados como identificadores diferentes.
     */
    for (FString& Symbol : TargetSymbols)
    {
        Symbol = Symbol.ToUpper().TrimStartAndEnd();
    }


    // ------------------------------------------------------------
    // 4. CONSTRUCCIÓN DEL CONJUNTO DE BÚSQUEDA
    // ------------------------------------------------------------

    /*
     * AllowedSymbols almacena los símbolos permitidos en un TSet.
     *
     * Antes, este conjunto se reconstruía en cada mensaje recibido
     * desde Binance.
     *
     * Ahora se construye una sola vez durante Initialize().
     *
     * Esto evita realizar el mismo trabajo repetidamente
     * mientras el WebSocket está procesando actualizaciones.
     *
     * El conjunto se considera de solo lectura después
     * de finalizar la inicialización.
     */
    AllowedSymbols.Reset();

    for (const FString& Symbol : TargetSymbols)
    {
        if (!Symbol.IsEmpty())
        {
            AllowedSymbols.Add(Symbol);
        }
    }


    // ------------------------------------------------------------
    // 5. CONFIGURACIÓN DEL INTERVALO DE ACTUALIZACIÓN
    // ------------------------------------------------------------

    /*
     * Limita UpdateInterval al rango de 0.2 a 0.3 segundos.
     *
     * FMath::Clamp devuelve:
     * - 0.2 si el valor es inferior a 0.2.
     * - 0.3 si el valor es superior a 0.3.
     * - El mismo valor si está dentro del rango.
     *
     * Este intervalo controla la distribución de actualizaciones
     * hacia los sistemas visuales.
     */
    UpdateInterval = FMath::Clamp(UpdateInterval, 0.2f, 0.3f);


    // ------------------------------------------------------------
    // 6. INICIALIZACIÓN DE LA CONEXIÓN Y DEL TICKER
    // ------------------------------------------------------------

    /*
     * Primero iniciamos la conexión con Binance.
     *
     * Después registramos el ticker que distribuirá las actualizaciones
     * recibidas a los sistemas que estén suscritos al delegado.
     */
    ConnectWebSocket();

    SetupTicker();
}


/**
 * Deinitialize
 *
 * Libera los recursos del subsistema antes de su destrucción.
 *
 * Es importante retirar los callbacks periódicos y cerrar
 * los recursos de red para evitar que sigan trabajando
 * cuando el GameInstance ya está finalizando.
 */
void UCryptoFinanceSubsystem::Deinitialize()
{
    // ------------------------------------------------------------
    // 1. RETIRAR EL TICKER
    // ------------------------------------------------------------

    /*
     * Comprobamos si existe un ticker registrado.
     *
     * Si el handle es válido, retiramos su callback del CoreTicker.
     *
     * Esto evita que HandleTicker siga intentando ejecutarse
     * después de que el subsistema haya comenzado a destruirse.
     */
    if (TickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);

        // Reiniciamos el handle para indicar que ya no existe un ticker activo.
        TickerHandle.Reset();
    }


    // ------------------------------------------------------------
    // 2. CIERRE DE LA CONEXIÓN WEBSOCKET
    // ------------------------------------------------------------

    /*
     * Si existe una referencia válida al WebSocket y la conexión
     * se encuentra activa, solicitamos su cierre.
     *
     * Close() solicita finalizar la conexión.
     *
     * Esta operación no debe interpretarse como una garantía
     * de que todos los callbacks pendientes hayan terminado.
     *
     * El manejo completo de callbacks durante la destrucción
     * será una de las siguientes mejoras de seguridad del subsistema.
     */
    if (WebSocket.IsValid())
    {
        if (WebSocket->IsConnected())
        {
            WebSocket->Close();
        }

        // Liberamos nuestra referencia compartida al cliente WebSocket.
        WebSocket.Reset();
    }


    // Ejecutamos la limpieza de la clase base.
    Super::Deinitialize();
}


/**
 * SetupTicker
 *
 * Registra el callback periódico que comprueba si existen
 * datos nuevos para distribuir a los sistemas visuales.
 *
 * FTSTicker permite ejecutar una función cada determinado intervalo
 * sin depender del Tick de un Actor.
 */
void UCryptoFinanceSubsystem::SetupTicker()
{
    // ------------------------------------------------------------
    // 1. ELIMINAR UN TICKER ANTERIOR
    // ------------------------------------------------------------

    /*
     * Antes de registrar uno nuevo, comprobamos si ya existe.
     *
     * Esto evita acumular múltiples callbacks ejecutando
     * la misma función.
     */
    if (TickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);

        TickerHandle.Reset();
    }


    // ------------------------------------------------------------
    // 2. VALIDAR EL INTERVALO
    // ------------------------------------------------------------

    /*
     * Volvemos a limitar el intervalo por seguridad.
     *
     * Esto garantiza que el ticker utilice un valor válido
     * incluso si SetupTicker() se llama desde otra parte del código.
     */
    const float ClampedInterval = FMath::Clamp(UpdateInterval, 0.2f, 0.3f);


    // ------------------------------------------------------------
    // 3. REGISTRAR EL CALLBACK
    // ------------------------------------------------------------

    /*
     * AddTicker registra HandleTicker como función periódica.
     *
     * CreateUObject vincula el callback con este objeto UObject.
     *
     * El ticker recibe el tiempo transcurrido desde la actualización
     * anterior mediante DeltaTime.
     *
     * Ejemplo:
     * 0.25 segundos = 250 milisegundos.
     */
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(
            this,
            &UCryptoFinanceSubsystem::HandleTicker
        ),
        ClampedInterval
    );
}


/**
 * ConnectWebSocket
 *
 * Crea y conecta el cliente WebSocket con Binance.
 *
 * Se registran los callbacks correspondientes a los diferentes
 * eventos de conexión antes de iniciar la comunicación.
 *
 * IMPORTANTE:
 * El callback de recepción utiliza una captura de this.
 * En esta versión todavía debemos endurecer el ciclo de vida
 * para garantizar que no se ejecute sobre un subsistema destruido.
 */
void UCryptoFinanceSubsystem::ConnectWebSocket()
{
    // ------------------------------------------------------------
    // 1. DIRECCIÓN DEL SERVIDOR
    // ------------------------------------------------------------

    /*
     * Endpoint de Binance para recibir actualizaciones miniTicker
     * de todos los pares del mercado en formato de arreglo JSON.
     *
     * El filtro AllowedSymbols permite procesar únicamente
     * los activos configurados en TargetSymbols.
     *
     * Actualmente recibimos el flujo general del mercado
     * y descartamos los símbolos que no necesitamos.
     */
    const FString ServerUrl =
        TEXT("wss://stream.binance.com:9443/ws/!miniTicker@arr");


    // ------------------------------------------------------------
    // 2. CREACIÓN DEL CLIENTE WEBSOCKET
    // ------------------------------------------------------------

    /*
     * CreateWebSocket crea una instancia del cliente WebSocket
     * utilizando el módulo WebSockets de Unreal Engine.
     *
     * La referencia se almacena en el miembro WebSocket
     * para poder consultar y controlar la conexión posteriormente.
     */
    WebSocket = FWebSocketsModule::Get().CreateWebSocket(ServerUrl);


    // ------------------------------------------------------------
    // 3. CALLBACK: CONEXIÓN EXITOSA
    // ------------------------------------------------------------

    /*
     * OnConnected se ejecuta cuando el cliente logra establecer
     * correctamente la conexión con Binance.
     *
     * Por ahora registramos el evento en el Output Log.
     *
     * Más adelante podemos utilizar este evento para actualizar
     * un estado interno de conexión.
     */
    WebSocket->OnConnected().AddLambda([]()
        {
            UE_LOG(
                LogTemp,
                Log,
                TEXT("[Vantage] Conectado al WebSocket de Binance (!miniTicker@arr).")
            );
        });


    // ------------------------------------------------------------
    // 4. CALLBACK: ERROR DE CONEXIÓN
    // ------------------------------------------------------------

    /*
     * OnConnectionError se ejecuta cuando ocurre un problema
     * durante el establecimiento de la conexión.
     *
     * Error contiene el mensaje proporcionado por el cliente WebSocket.
     *
     * Actualmente registramos el error para facilitar el diagnóstico.
     * Todavía no existe un mecanismo de reconexión automática.
     */
    WebSocket->OnConnectionError().AddLambda([](const FString& Error)
        {
            UE_LOG(
                LogTemp,
                Error,
                TEXT("[Vantage] Error de conexión WebSocket: %s"),
                *Error
            );
        });


    // ------------------------------------------------------------
    // 5. CALLBACK: CONEXIÓN CERRADA
    // ------------------------------------------------------------

    /*
     * OnClosed se ejecuta cuando la conexión WebSocket se cierra.
     *
     * StatusCode:
     * Código de cierre informado por el WebSocket.
     *
     * Reason:
     * Descripción del motivo del cierre.
     *
     * bWasClean:
     * Indica si el cierre fue considerado limpio.
     *
     * Este callback permite conocer cuándo la conexión deja de estar activa.
     */
    WebSocket->OnClosed().AddLambda(
        [](int32 StatusCode, const FString& Reason, bool bWasClean)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[Vantage] WebSocket cerrado. Codigo: %d. Razon: %s. Cierre limpio: %s"),
                StatusCode,
                *Reason,
                bWasClean ? TEXT("Si") : TEXT("No")
            );
        }
    );


    // ------------------------------------------------------------
    // 6. CALLBACK: RECEPCIÓN DE MENSAJES
    // ------------------------------------------------------------

    /*
     * OnMessage se ejecuta cada vez que el WebSocket recibe
     * un mensaje de texto desde Binance.
     *
     * MessageString contiene el mensaje JSON completo.
     *
     * El callback realiza las siguientes operaciones:
     *
     * 1. Deserializa el mensaje JSON.
     * 2. Comprueba que el mensaje sea un arreglo válido.
     * 3. Recorre los activos recibidos.
     * 4. Extrae los campos necesarios.
     * 5. Filtra los símbolos que no pertenecen a AllowedSymbols.
     * 6. Construye FCryptoAssetData.
     * 7. Actualiza AssetCache.
     * 8. Marca que existen datos nuevos para distribuir.
     *
     * NOTA DE CONCURRENCIA:
     * No asumimos aquí que el callback se ejecute en el GameThread.
     * Por eso el acceso al caché y a la bandera se protege con Mutex.
     *
     * La captura [this] sigue siendo un punto pendiente de endurecimiento
     * para el cierre del subsistema.
     */
    WebSocket->OnMessage().AddLambda([this](const FString& MessageString)
        {
            // --------------------------------------------------------
            // 6.1. PREPARAR EL CONTENEDOR JSON
            // --------------------------------------------------------

            /*
             * Binance envía el stream !miniTicker@arr como un arreglo JSON.
             *
             * Cada elemento del arreglo representa un par de mercado.
             *
             * TSharedPtr<FJsonValue> permite almacenar los valores
             * JSON recibidos mediante el sistema de referencias compartidas.
             */
            TArray<TSharedPtr<FJsonValue>> JsonArray;


            // Creamos un lector para interpretar el texto recibido.
            TSharedRef<TJsonReader<>> Reader =
                TJsonReaderFactory<>::Create(MessageString);


            // --------------------------------------------------------
            // 6.2. DESERIALIZAR EL MENSAJE
            // --------------------------------------------------------

            /*
             * Deserialize transforma el texto JSON en estructuras
             * que Unreal Engine puede consultar desde C++.
             *
             * Si el mensaje no representa un arreglo JSON válido,
             * abandonamos el procesamiento.
             */
            if (!FJsonSerializer::Deserialize(Reader, JsonArray))
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("[Vantage] No se pudo deserializar un mensaje de Binance.")
                );

                return;
            }


            // --------------------------------------------------------
            // 6.3. PREPARAR EL ESTADO DE ACTUALIZACIÓN
            // --------------------------------------------------------

            /*
             * Indica si este mensaje contenía al menos un activo
             * que perteneciera al catálogo configurado.
             *
             * Solo marcamos bNewDataReceived si realmente modificamos
             * el caché con información relevante.
             */
            bool bUpdatedAny = false;


            // --------------------------------------------------------
            // 6.4. PROTEGER EL ACCESO AL CACHÉ
            // --------------------------------------------------------

            /*
             * FScopeLock adquiere Mutex al entrar en este ámbito.
             *
             * Mientras el bloqueo esté activo, otras funciones que
             * utilicen el mismo Mutex no podrán acceder simultáneamente
             * a AssetCache ni a bNewDataReceived.
             *
             * El bloqueo se libera automáticamente al finalizar
             * el callback o al salir del ámbito.
             */
            {
                FScopeLock Lock(&Mutex);


                // ----------------------------------------------------
                // 6.5. RECORRER LOS ACTIVOS DEL MENSAJE
                // ----------------------------------------------------

                for (const TSharedPtr<FJsonValue>& Value : JsonArray)
                {
                    /*
                     * Comprobamos que el valor JSON sea válido
                     * antes de intentar interpretarlo como objeto.
                     */
                    if (!Value.IsValid())
                    {
                        continue;
                    }


                    /*
                     * Cada elemento del arreglo debería ser un objeto JSON.
                     *
                     * AsObject() devuelve una referencia compartida al objeto
                     * cuando el tipo recibido es compatible.
                     */
                    const TSharedPtr<FJsonObject> JsonObject = Value->AsObject();


                    // Ignoramos elementos que no sean objetos válidos.
                    if (!JsonObject.IsValid())
                    {
                        continue;
                    }


                    // ------------------------------------------------
                    // 6.6. EXTRAER EL SÍMBOLO
                    // ------------------------------------------------

                    /*
                     * El campo "s" identifica el par de negociación.
                     *
                     * Ejemplo:
                     * "s": "BTCUSDT"
                     *
                     * TryGetStringField permite comprobar que el campo
                     * exista y sea una cadena válida antes de utilizarlo.
                     */
                    FString Symbol;

                    if (!JsonObject->TryGetStringField(TEXT("s"), Symbol))
                    {
                        continue;
                    }


                    // Normalizamos el identificador recibido.
                    Symbol = Symbol.ToUpper().TrimStartAndEnd();


                    // Si el símbolo está vacío, no podemos identificar el activo.
                    if (Symbol.IsEmpty())
                    {
                        continue;
                    }


                    // ------------------------------------------------
                    // 6.7. FILTRAR LOS SÍMBOLOS
                    // ------------------------------------------------

                    /*
                     * AllowedSymbols contiene los símbolos que Vantage
                     * desea almacenar y visualizar.
                     *
                     * Contains comprueba si el símbolo recibido pertenece
                     * al conjunto permitido.
                     *
                     * Si no pertenece, descartamos el activo.
                     *
                     * Este conjunto se construyó durante Initialize(),
                     * por lo que no necesitamos reconstruirlo en cada mensaje.
                     */
                    if (AllowedSymbols.Num() > 0 &&
                        !AllowedSymbols.Contains(Symbol))
                    {
                        continue;
                    }


                    // ------------------------------------------------
                    // 6.8. EXTRAER LOS VALORES NUMÉRICOS
                    // ------------------------------------------------

                    /*
                     * Binance entrega los valores numéricos del miniTicker
                     * como cadenas de texto.
                     *
                     * TryGetStringField evita intentar convertir campos
                     * que no existen o que tienen un tipo incorrecto.
                     *
                     * Campos utilizados:
                     *
                     * c = Último precio.
                     * o = Precio de apertura del período.
                     * h = Precio máximo.
                     * l = Precio mínimo.
                     * v = Volumen negociado.
                     */
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


                    /*
                     * Si falta alguno de los campos esenciales,
                     * descartamos el registro para evitar almacenar
                     * información incompleta como si fuera válida.
                     */
                    if (!bHasLastPrice ||
                        !bHasOpenPrice ||
                        !bHasHighPrice ||
                        !bHasLowPrice ||
                        !bHasVolume)
                    {
                        continue;
                    }


                    // ------------------------------------------------
                    // 6.9. CONVERTIR TEXTO A DOUBLE
                    // ------------------------------------------------

                    /*
                     * FCString::Atod convierte una cadena numérica
                     * en un valor de tipo double.
                     *
                     * Utilizamos double para conservar precisión decimal
                     * en los precios y volúmenes.
                     *
                     * La validación de rangos y valores numéricos inválidos
                     * será una mejora adicional del procesamiento de datos.
                     */
                    const double LastPrice = FCString::Atod(*LastPriceString);
                    const double OpenPrice = FCString::Atod(*OpenPriceString);
                    const double HighPrice = FCString::Atod(*HighPriceString);
                    const double LowPrice = FCString::Atod(*LowPriceString);
                    const double Volume = FCString::Atod(*VolumeString);


                    // ------------------------------------------------
                    // 6.10. CONSTRUIR LA ESTRUCTURA DEL ACTIVO
                    // ------------------------------------------------

                    /*
                     * Creamos una estructura FCryptoAssetData.
                     *
                     * Esta estructura agrupa todos los valores necesarios
                     * para representar el estado actual del activo.
                     */
                    FCryptoAssetData AssetData;


                    // Identificador del mercado.
                    AssetData.Symbol = Symbol;

                    // Último precio informado.
                    AssetData.LastPrice = LastPrice;

                    // Precio máximo.
                    AssetData.HighPrice = HighPrice;

                    // Precio mínimo.
                    AssetData.LowPrice = LowPrice;

                    // Volumen negociado.
                    AssetData.Volume = Volume;


                    // ------------------------------------------------
                    // 6.11. CALCULAR VARIACIONES DE PRECIO
                    // ------------------------------------------------

                    /*
                     * Variación absoluta:
                     *
                     * Último precio - Precio de apertura.
                     *
                     * Un resultado positivo indica una variación al alza.
                     * Un resultado negativo indica una variación a la baja.
                     */
                    AssetData.PriceChange = LastPrice - OpenPrice;


                    /*
                     * Variación porcentual:
                     *
                     * ((Último precio - Precio de apertura) / Precio de apertura) * 100.
                     *
                     * Evitamos dividir por cero cuando OpenPrice no es positivo.
                     */
                    AssetData.PriceChangePercent = (OpenPrice > 0.0)
                        ? ((LastPrice - OpenPrice) / OpenPrice) * 100.0
                        : 0.0;


                    // ------------------------------------------------
                    // 6.12. ACTUALIZAR EL CACHÉ
                    // ------------------------------------------------

                    /*
                     * Add almacena AssetData utilizando Symbol como clave.
                     *
                     * Si el símbolo todavía no existe, se agrega una entrada.
                     *
                     * Si ya existe, se reemplaza su valor anterior.
                     *
                     * De esta manera AssetCache mantiene la última información
                     * disponible para cada par de mercado.
                     */
                    AssetCache.Add(Symbol, AssetData);


                    // Indicamos que el mensaje modificó información relevante.
                    bUpdatedAny = true;
                }


                // ----------------------------------------------------
                // 6.13. MARCAR QUE EXISTEN DATOS NUEVOS
                // ----------------------------------------------------

                /*
                 * Si al menos un activo fue actualizado,
                 * activamos la bandera de notificación.
                 *
                 * HandleTicker consultará esta bandera posteriormente
                 * para determinar si debe emitir OnCryptoDataUpdated.
                 */
                if (bUpdatedAny)
                {
                    bNewDataReceived = true;
                }
            }

            /*
             * Al finalizar este bloque, FScopeLock libera Mutex.
             *
             * Así otras funciones del subsistema pueden volver
             * a consultar o modificar AssetCache.
             */
        });


    // ------------------------------------------------------------
    // 7. INICIAR LA CONEXIÓN
    // ------------------------------------------------------------

    /*
     * Connect inicia el proceso de conexión con Binance.
     *
     * Los callbacks ya fueron registrados, por lo que el sistema
     * puede recibir los eventos de conexión y mensajes.
     */
    WebSocket->Connect();
}


/**
 * HandleTicker
 *
 * Se ejecuta periódicamente mediante FTSTicker.
 *
 * Su responsabilidad es comprobar si el subsistema recibió
 * nuevos datos desde Binance y, si corresponde, distribuirlos
 * mediante OnCryptoDataUpdated.
 *
 * La emisión se realiza fuera del bloqueo Mutex para evitar
 * mantener bloqueado el caché mientras otros sistemas ejecutan
 * sus propias funciones al recibir el delegado.
 *
 * @param DeltaTime Tiempo transcurrido desde la última ejecución.
 *
 * @return true para mantener el ticker registrado.
 */
bool UCryptoFinanceSubsystem::HandleTicker(float DeltaTime)
{
    // Indica si debemos emitir una actualización.
    bool bShouldBroadcast = false;


    /*
     * SnapshotList almacena una copia de los datos del caché.
     *
     * El término Snapshot hace referencia a una fotografía
     * del estado de los activos en un momento determinado.
     */
    TArray<FCryptoAssetData> SnapshotList;


    // ------------------------------------------------------------
    // 1. CONSULTAR SI EXISTEN DATOS NUEVOS
    // ------------------------------------------------------------

    {
        /*
         * Protegemos la lectura de la bandera y la copia del caché.
         *
         * De esta manera el callback WebSocket no puede modificar
         * AssetCache mientras estamos generando SnapshotList.
         */
        FScopeLock Lock(&Mutex);


        if (bNewDataReceived)
        {
            // Consumimos la notificación pendiente.
            bNewDataReceived = false;

            // Confirmamos que existe una actualización para distribuir.
            bShouldBroadcast = true;


            /*
             * GenerateValueArray copia los valores del TMap
             * dentro del array de salida.
             *
             * El orden de los elementos no está garantizado.
             */
            AssetCache.GenerateValueArray(SnapshotList);
        }
    }


    // ------------------------------------------------------------
    // 2. DISTRIBUIR LOS DATOS
    // ------------------------------------------------------------

    /*
     * Emitimos el evento fuera del bloqueo.
     *
     * Esto es importante porque los objetos suscritos al delegado
     * pueden ejecutar operaciones adicionales al recibir la actualización.
     *
     * No queremos mantener Mutex bloqueado durante esas operaciones.
     */
    if (bShouldBroadcast)
    {
        OnCryptoDataUpdated.Broadcast(SnapshotList);
    }


    /*
     * Devolvemos true para indicarle a FTSTicker que debe
     * mantener este callback registrado.
     */
    return true;
}


/**
 * GetAssetData
 *
 * Busca un activo específico dentro del caché.
 *
 * Utiliza TMap::Find() para realizar una búsqueda por clave.
 *
 * Esta función es útil cuando un actor necesita consultar
 * un único activo sin copiar toda la colección.
 *
 * @param Symbol Símbolo que identifica al activo.
 * @param OutData Estructura donde se copiarán los datos encontrados.
 *
 * @return true si el activo existe.
 * @return false si no existe o el símbolo está vacío.
 */
bool UCryptoFinanceSubsystem::GetAssetData(
    const FString& Symbol,
    FCryptoAssetData& OutData
)
{
    // ------------------------------------------------------------
    // 1. NORMALIZAR EL IDENTIFICADOR
    // ------------------------------------------------------------

    /*
     * Normalizamos el símbolo recibido para que coincida
     * con las claves almacenadas en AssetCache.
     */
    const FString NormalizedSymbol =
        Symbol.ToUpper().TrimStartAndEnd();


    // No podemos buscar una clave vacía.
    if (NormalizedSymbol.IsEmpty())
    {
        return false;
    }


    // ------------------------------------------------------------
    // 2. BUSCAR EL ACTIVO EN EL CACHÉ
    // ------------------------------------------------------------

    /*
     * Protegemos la búsqueda porque AssetCache puede estar
     * siendo actualizado desde el callback WebSocket.
     */
    FScopeLock Lock(&Mutex);


    /*
     * Find devuelve un puntero al valor asociado a la clave.
     *
     * Si existe, copiamos su contenido hacia OutData.
     *
     * La copia permite que el consumidor utilice los datos
     * sin conservar un puntero interno al TMap.
     */
    if (const FCryptoAssetData* Found = AssetCache.Find(NormalizedSymbol))
    {
        OutData = *Found;

        return true;
    }


    // El símbolo no se encuentra almacenado.
    return false;
}


/**
 * GetAllAssets
 *
 * Copia todos los activos disponibles en AssetCache
 * hacia el array proporcionado por el consumidor.
 *
 * IMPORTANTE:
 * El orden de salida no está garantizado porque AssetCache
 * utiliza TMap como estructura de almacenamiento.
 *
 * Para obtener un orden estable, utilizar GetOrderedTargetAssets().
 *
 * @param OutAssets Array donde se almacenarán los activos.
 */
void UCryptoFinanceSubsystem::GetAllAssets(
    TArray<FCryptoAssetData>& OutAssets
)
{
    // Eliminamos los elementos que pudiera contener el array anterior.
    OutAssets.Reset();


    /*
     * Protegemos la lectura del caché mientras generamos
     * la copia de sus valores.
     */
    FScopeLock Lock(&Mutex);


    /*
     * GenerateValueArray copia todos los valores del TMap.
     *
     * No copia las claves porque los símbolos ya están incluidos
     * dentro de FCryptoAssetData::Symbol.
     */
    AssetCache.GenerateValueArray(OutAssets);
}


/**
 * GetOrderedTargetAssets
 *
 * Devuelve los activos respetando el orden configurado
 * en TargetSymbols.
 *
 * Esta función es especialmente útil para ACryptoTerminalActor,
 * ya que permite paginar los activos sin depender del orden
 * interno de AssetCache.
 *
 * Si un activo todavía no llegó desde Binance,
 * se agrega una estructura provisional con su símbolo.
 *
 * @param OutOrderedAssets Array que recibirá los activos ordenados.
 */
void UCryptoFinanceSubsystem::GetOrderedTargetAssets(
    TArray<FCryptoAssetData>& OutOrderedAssets
)
{
    // Eliminamos el contenido anterior del array.
    OutOrderedAssets.Reset();


    /*
     * Reservamos capacidad para evitar reasignaciones innecesarias
     * mientras agregamos los activos.
     *
     * TargetSymbols determina la cantidad máxima esperada.
     */
    OutOrderedAssets.Reserve(TargetSymbols.Num());


    /*
     * Protegemos el acceso a AssetCache.
     *
     * TargetSymbols y AllowedSymbols se consideran de solo lectura
     * después de Initialize().
     */
    FScopeLock Lock(&Mutex);


    // ------------------------------------------------------------
    // RECORRER EL CATÁLOGO EN SU ORDEN ORIGINAL
    // ------------------------------------------------------------

    for (const FString& Symbol : TargetSymbols)
    {
        // Ignoramos identificadores vacíos.
        if (Symbol.IsEmpty())
        {
            continue;
        }


        /*
         * Buscamos el símbolo dentro del caché.
         *
         * Si existe, agregamos una copia de los datos actuales.
         */
        if (const FCryptoAssetData* Found = AssetCache.Find(Symbol))
        {
            OutOrderedAssets.Add(*Found);
        }
        else
        {
            /*
             * Si todavía no recibimos información de Binance,
             * generamos una estructura provisional.
             *
             * El símbolo queda reservado en su posición correspondiente.
             *
             * Esto permite mantener una distribución estable
             * de los slots aunque algunos activos no tengan datos.
             */
            FCryptoAssetData Placeholder;

            Placeholder.Symbol = Symbol;

            OutOrderedAssets.Add(Placeholder);
        }
    }
}
