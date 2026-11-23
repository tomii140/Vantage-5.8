
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "IWebSocket.h"
#include "CryptoFinanceSubsystem.generated.h"

/**
 * FCryptoAssetData
 *
 * Estructura que representa la información de mercado de una criptomoneda.
 *
 * Cada instancia almacena una fotografía de los últimos datos recibidos
 * desde Binance para un determinado par de negociación.
 *
 * Ejemplo:
 * BTCUSDT representa el mercado de Bitcoin frente a Tether.
 *
 * Los valores numéricos se inicializan en 0.0 para evitar utilizar
 * valores sin inicializar antes de recibir información del servidor.
 *
 * Esta estructura se utiliza tanto en C++ como en Blueprints.
 */
USTRUCT(BlueprintType)
struct FCryptoAssetData
{
    GENERATED_BODY()

    /**
     * Símbolo identificador del par de mercado.
     *
     * Ejemplo: BTCUSDT, ETHUSDT, SOLUSDT.
     *
     * Se almacena en mayúsculas para mantener una identificación uniforme
     * dentro del caché del subsistema.
     */
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    FString Symbol;

    /**
     * Último precio registrado para el activo.
     *
     * Se obtiene del campo "c" del mensaje miniTicker de Binance.
     */
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double LastPrice = 0.0;

    /**
     * Variación absoluta del precio durante el período informado.
     *
     * Fórmula:
     * LastPrice - OpenPrice
     */
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double PriceChange = 0.0;

    /**
     * Variación porcentual del precio durante el período informado.
     *
     * Fórmula:
     * ((LastPrice - OpenPrice) / OpenPrice) * 100
     *
     * Se evita la división por cero cuando OpenPrice no es válido.
     */
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double PriceChangePercent = 0.0;

    /**
     * Precio máximo registrado durante el período informado.
     *
     * Se obtiene del campo "h" del mensaje miniTicker.
     */
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double HighPrice = 0.0;

    /**
     * Precio mínimo registrado durante el período informado.
     *
     * Se obtiene del campo "l" del mensaje miniTicker.
     */
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double LowPrice = 0.0;

    /**
     * Volumen negociado durante el período informado.
     *
     * Se obtiene del campo "v" del mensaje miniTicker.
     */
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double Volume = 0.0;
};


/**
 * Delegado dinámico multicast.
 *
 * Se ejecuta cuando el subsistema detecta nuevos datos de mercado
 * y llega el momento de distribuir una actualización.
 *
 * El parámetro UpdatedAssets contiene una copia de los datos disponibles
 * en el caché en el momento de generar la actualización.
 *
 * Puede vincularse desde C++ y desde Blueprints.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnCryptoDataUpdated,
    const TArray<FCryptoAssetData>&,
    UpdatedAssets
);


/**
 * UCryptoFinanceSubsystem
 *
 * Subsistema central encargado de recibir, procesar y distribuir
 * información financiera en tiempo real.
 *
 * Hereda de UGameInstanceSubsystem, por lo que su ciclo de vida
 * está asociado al GameInstance y no a un Actor individual del nivel.
 *
 * RESPONSABILIDADES PRINCIPALES:
 *
 * 1. Administrar la conexión WebSocket con Binance.
 * 2. Recibir y deserializar mensajes JSON.
 * 3. Filtrar los símbolos configurados en TargetSymbols.
 * 4. Mantener los últimos datos recibidos en AssetCache.
 * 5. Proteger el acceso concurrente al caché mediante FCriticalSection.
 * 6. Emitir actualizaciones mediante un ticker periódico.
 * 7. Proporcionar funciones de consulta para los actores visuales.
 *
 * ARQUITECTURA:
 *
 * Binance WebSocket
 *        |
 *        v
 * Procesamiento JSON
 *        |
 *        v
 * Filtro de símbolos
 *        |
 *        v
 * AssetCache
 *        |
 *        v
 * OnCryptoDataUpdated
 *        |
 *        v
 * Actores visuales de Vantage
 *
 * NOTA:
 * Esta versión mantiene la conexión inicial y el procesamiento de datos.
 * La reconexión automática y el endurecimiento completo del ciclo de vida
 * de los callbacks se incorporarán en una etapa posterior.
 */
UCLASS()
class VANTAGE_API UCryptoFinanceSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:

    /**
     * Inicializa el subsistema.
     *
     * Se ejecuta cuando Unreal Engine crea el subsistema asociado
     * al GameInstance.
     *
     * Responsabilidades:
     * - Inicializar la configuración de símbolos.
     * - Normalizar los símbolos configurados.
     * - Construir el conjunto de búsqueda AllowedSymbols.
     * - Configurar el intervalo de actualización.
     * - Establecer la conexión WebSocket.
     * - Registrar el ticker periódico.
     *
     * @param Collection Colección de subsistemas utilizada por Unreal Engine.
     */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;


    /**
     * Libera los recursos utilizados por el subsistema.
     *
     * Se ejecuta cuando el GameInstance está finalizando
     * o cuando Unreal Engine destruye el subsistema.
     *
     * Responsabilidades:
     * - Retirar el ticker registrado.
     * - Cerrar la conexión WebSocket.
     * - Liberar la referencia al WebSocket.
     * - Ejecutar la implementación de la clase base.
     */
    virtual void Deinitialize() override;


    /**
     * Evento público de actualización de datos financieros.
     *
     * Los actores o Blueprints pueden suscribirse a este evento
     * para recibir información actualizada sin consultar el caché
     * constantemente en cada frame.
     */
    UPROPERTY(BlueprintAssignable, Category = "Vantage|Events")
    FOnCryptoDataUpdated OnCryptoDataUpdated;


    /**
     * Catálogo ordenado de símbolos que Vantage debe visualizar.
     *
     * Se puede configurar desde el editor o mediante Blueprint.
     *
     * El orden de esta lista también determina el orden de salida
     * de GetOrderedTargetAssets().
     *
     * Ejemplo:
     * BTCUSDT, ETHUSDT, BNBUSDT, SOLUSDT...
     *
     * Los símbolos se normalizan durante Initialize().
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Catalog")
    TArray<FString> TargetSymbols;


    /**
     * Intervalo de distribución de actualizaciones, expresado en segundos.
     *
     * Ejemplo:
     * 0.25 segundos = 250 milisegundos.
     *
     * Este intervalo regula la frecuencia de emisión del delegado.
     * No modifica la frecuencia con la que Binance envía sus mensajes.
     *
     * El valor se limita internamente al rango de 0.2 a 0.3 segundos.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Vantage|Performance",
        meta = (ClampMin = "0.2", ClampMax = "0.3")
    )
    float UpdateInterval = 0.25f;


    /**
     * Obtiene los datos almacenados para un símbolo específico.
     *
     * Realiza una búsqueda por clave dentro de AssetCache mediante TMap::Find().
     *
     * La consulta está protegida por Mutex para evitar leer el mapa
     * mientras otra operación lo está modificando.
     *
     * @param Symbol Símbolo que se desea consultar.
     * @param OutData Estructura donde se copiarán los datos encontrados.
     *
     * @return true si el símbolo existe en el caché.
     * @return false si el símbolo no existe o está vacío.
     */
    UFUNCTION(BlueprintCallable, Category = "Vantage|Finance")
    bool GetAssetData(
        const FString& Symbol,
        FCryptoAssetData& OutData
    );


    /**
     * Devuelve todos los activos actualmente almacenados.
     *
     * Los elementos se copian desde AssetCache hacia OutAssets.
     *
     * IMPORTANTE:
     * TMap no garantiza un orden específico de iteración.
     * Si se necesita respetar TargetSymbols, utilizar
     * GetOrderedTargetAssets().
     *
     * @param OutAssets Array que recibirá los activos disponibles.
     */
    UFUNCTION(BlueprintCallable, Category = "Vantage|Finance")
    void GetAllAssets(TArray<FCryptoAssetData>& OutAssets);


    /**
     * Devuelve los activos respetando exactamente el orden de TargetSymbols.
     *
     * Si un activo todavía no recibió datos desde Binance,
     * se agrega una estructura provisional con su símbolo y valores numéricos
     * inicializados en cero.
     *
     * Esto permite mantener posiciones estables en la interfaz,
     * incluso cuando algunos activos todavía no tienen cotización.
     *
     * @param OutOrderedAssets Array de salida con los activos ordenados.
     */
    UFUNCTION(BlueprintCallable, Category = "Vantage|Finance")
    void GetOrderedTargetAssets(
        TArray<FCryptoAssetData>& OutOrderedAssets
    );


private:

    /**
     * Crea e inicia la conexión WebSocket con Binance.
     *
     * Registra los callbacks de conexión, error, cierre y recepción
     * de mensajes antes de iniciar Connect().
     */
    void ConnectWebSocket();


    /**
     * Registra el ticker periódico encargado de distribuir actualizaciones.
     *
     * Si ya existe un ticker registrado, primero lo elimina
     * para evitar registrar múltiples instancias.
     */
    void SetupTicker();


    /**
     * Callback ejecutado por FTSTicker.
     *
     * Comprueba si llegaron datos nuevos desde la última emisión.
     * Si corresponde, genera una copia del caché y distribuye
     * el evento OnCryptoDataUpdated.
     *
     * @param DeltaTime Tiempo transcurrido desde la última actualización.
     *
     * @return true para mantener el ticker activo.
     */
    bool HandleTicker(float DeltaTime);


private:

    /**
     * Referencia al cliente WebSocket utilizado para comunicarse con Binance.
     *
     * Se almacena como TSharedPtr porque el cliente WebSocket
     * utiliza el sistema de referencias compartidas de Unreal Engine.
     */
    TSharedPtr<IWebSocket> WebSocket;


    /**
     * Caché central de activos financieros.
     *
     * Clave:
     * Símbolo del activo, por ejemplo BTCUSDT.
     *
     * Valor:
     * Estructura FCryptoAssetData con los últimos datos recibidos.
     *
     * Se utiliza TMap porque permite localizar un activo mediante
     * su símbolo sin recorrer secuencialmente todos los elementos.
     *
     * El acceso está protegido por Mutex.
     */
    TMap<FString, FCryptoAssetData> AssetCache;


    /**
     * Conjunto de símbolos permitidos para el procesamiento de mensajes.
     *
     * Se construye una sola vez durante Initialize(), a partir
     * de TargetSymbols ya normalizado.
     *
     * TSet permite comprobar rápidamente si un símbolo pertenece
     * al catálogo configurado.
     *
     * Después de Initialize() se considera de solo lectura.
     * Si en el futuro permitimos modificar TargetSymbols durante
     * la ejecución, habrá que reconstruir este conjunto de forma segura.
     */
    TSet<FString> AllowedSymbols;


    /**
     * Mutex utilizado para proteger los datos compartidos.
     *
     * Protege principalmente:
     * - AssetCache.
     * - bNewDataReceived.
     *
     * Permite que las operaciones de lectura y escritura no accedan
     * simultáneamente a esas estructuras desde distintos hilos.
     *
     * FScopeLock adquiere el bloqueo al entrar en el ámbito
     * y lo libera automáticamente al salir.
     */
    FCriticalSection Mutex;


    /**
     * Identificador del ticker registrado en FTSTicker.
     *
     * Permite retirar el callback cuando el subsistema finaliza
     * o cuando se necesita volver a configurar el intervalo.
     */
    FTSTicker::FDelegateHandle TickerHandle;


    /**
     * Indica si se recibió al menos una actualización de mercado
     * desde la última distribución del evento.
     *
     * Se modifica y consulta dentro de secciones protegidas por Mutex.
     *
     * Su objetivo es evitar emitir actualizaciones cuando no llegaron
     * datos nuevos desde Binance.
     */
    bool bNewDataReceived = false;
};
