#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "IWebSocket.h"
#include "Containers/Ticker.h"
#include "CryptoFinanceSubsystem.generated.h"

/**
 * FCryptoAssetData
 *
 * Almacena la última información de mercado recibida para un activo.
 * Los valores comienzan en cero para permitir la creación de placeholders.
 */
USTRUCT(BlueprintType)
struct FCryptoAssetData
{
    GENERATED_BODY()

    /** Identificador del mercado normalizado en mayúsculas. */
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    FString Symbol;

    /** Último precio informado por Binance. */
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double LastPrice = 0.0;

    /** Variación absoluta respecto del precio de apertura. */
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double PriceChange = 0.0;

    /** Variación porcentual respecto del precio de apertura. */
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double PriceChangePercent = 0.0;

    /** Precio máximo del período informado. */
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double HighPrice = 0.0;

    /** Precio mínimo del período informado. */
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double LowPrice = 0.0;

    /** Volumen negociado del período informado. */
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double Volume = 0.0;
};

/**
 * Evento emitido cuando hay información financiera nueva.
 *
 * El array se entrega como referencia constante durante la ejecución
 * del delegado. Los consumidores que necesiten conservarlo deben copiarlo.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnCryptoDataUpdated,
    const TArray<FCryptoAssetData>&,
    UpdatedAssets
);

/**
 * UCryptoFinanceSubsystem
 *
 * Subsistema central de recepción, almacenamiento y distribución
 * de datos financieros. Su ciclo de vida está asociado al GameInstance.
 *
 * ARQUITECTURA DE DATOS:
 *
 * - TargetSymbols: catálogo ordenado y fuente de verdad para la UI.
 * - AllowedSymbols: filtro de pertenencia rápida, construido una vez.
 * - AssetCache: almacenamiento principal y búsqueda por símbolo.
 *
 * ARQUITECTURA DE CONEXIÓN:
 *
 * - Mantiene una sola conexión activa o en proceso de conexión.
 * - Reintenta automáticamente ante errores y cierres inesperados.
 * - Utiliza backoff exponencial con un máximo de 30 segundos.
 * - Asigna una generación a cada conexión para descartar callbacks antiguos.
 * - Utiliza un watchdog para detectar conexiones sin recepción de mensajes.
 * - Cancela los tickers y las reconexiones durante Deinitialize().
 *
 * SEGURIDAD:
 *
 * - Los callbacks de red no modifican directamente el UObject.
 * - El procesamiento se transfiere al Game Thread.
 * - Las generaciones invalidan callbacks de conexiones anteriores.
 * - Mutex protege el caché y el estado de distribución.
 */
UCLASS()
class VANTAGE_API UCryptoFinanceSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:

    // ------------------------------------------------------------
    // CICLO DE VIDA
    // ------------------------------------------------------------

    /** Inicializa catálogo, filtros, conexión y tickers. */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    /** Detiene los tickers y cierra la conexión antes de destruirse. */
    virtual void Deinitialize() override;


    // ------------------------------------------------------------
    // EVENTOS PÚBLICOS
    // ------------------------------------------------------------

    /** Se emite cuando se detecta información nueva de los activos. */
    UPROPERTY(BlueprintAssignable, Category = "Vantage|Events")
    FOnCryptoDataUpdated OnCryptoDataUpdated;


    // ------------------------------------------------------------
    // CONFIGURACIÓN PÚBLICA
    // ------------------------------------------------------------

    /**
     * Catálogo de activos que Vantage debe procesar y visualizar.
     *
     * El orden determina el orden de las consultas y la paginación.
     * Los símbolos se normalizan y deduplican durante Initialize().
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Catalog")
    TArray<FString> TargetSymbols;

    /**
     * Intervalo de distribución del delegado, en segundos.
     * No controla la frecuencia de mensajes de Binance.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Vantage|Performance",
        meta = (ClampMin = "0.2", ClampMax = "0.3")
    )
    float UpdateInterval = 0.25f;


    // ------------------------------------------------------------
    // CONSULTAS PÚBLICAS
    // ------------------------------------------------------------

    /**
     * Busca un activo por símbolo.
     *
     * @param Symbol Identificador del mercado.
     * @param OutData Recibe una copia de los datos encontrados.
     * @return true si el activo existe.
     */
    UFUNCTION(BlueprintCallable, Category = "Vantage|Finance")
    bool GetAssetData(const FString& Symbol, FCryptoAssetData& OutData);

    /**
     * Devuelve los activos recibidos actualmente.
     * Respeta el orden de TargetSymbols y no incluye placeholders.
     */
    UFUNCTION(BlueprintCallable, Category = "Vantage|Finance")
    void GetAllAssets(TArray<FCryptoAssetData>& OutAssets);

    /**
     * Devuelve todos los símbolos configurados.
     * Incluye placeholders para activos todavía no recibidos.
     */
    UFUNCTION(BlueprintCallable, Category = "Vantage|Finance")
    void GetOrderedTargetAssets(TArray<FCryptoAssetData>& OutOrderedAssets);


private:

    // ------------------------------------------------------------
    // ESTADO DE CONEXIÓN
    // ------------------------------------------------------------

    /** Estados internos del ciclo de vida del WebSocket. */
    enum class EConnectionState : uint8
    {
        Disconnected,
        Connecting,
        Connected,
        Reconnecting,
        ShuttingDown
    };

    /** Estado actual de la conexión. Solo se modifica en Game Thread. */
    EConnectionState ConnectionState = EConnectionState::Disconnected;

    /** Identificador incremental para invalidar callbacks antiguos. */
    uint64 ConnectionGeneration = 0;

    /** Cantidad de reintentos consecutivos desde la última conexión exitosa. */
    int32 ReconnectAttempt = 0;

    /** Espera inicial antes del primer reintento. */
    static constexpr float ReconnectBaseDelay = 1.0f;

    /** Espera máxima entre intentos consecutivos. */
    static constexpr float ReconnectMaxDelay = 30.0f;


    // ------------------------------------------------------------
    // CONFIGURACIÓN DEL WATCHDOG
    // ------------------------------------------------------------

    /**
     * Frecuencia de comprobación del watchdog.
     * No representa la frecuencia de recepción de datos.
     */
    static constexpr float WatchdogCheckInterval = 2.0f;

    /**
     * Tiempo máximo sin mensajes antes de considerar inactiva
     * una conexión que ya había sido establecida.
     */
    static constexpr double MessageTimeoutSeconds = 15.0;

    /**
     * Tiempo máximo permitido para completar el handshake.
     * Evita que una conexión quede indefinidamente en Connecting.
     */
    static constexpr double ConnectionTimeoutSeconds = 15.0;

    /** Momento de recepción del último mensaje, medido en segundos. */
    double LastMessageTimeSeconds = 0.0;

    /** Momento en que comenzó el intento de conexión actual. */
    double ConnectionStartedAtSeconds = 0.0;


    // ------------------------------------------------------------
    // CONEXIÓN Y PROCESAMIENTO DE RED
    // ------------------------------------------------------------

    /** Crea el cliente WebSocket y registra sus callbacks. */
    void ConnectWebSocket();

    /** Programa un único intento de reconexión con backoff. */
    void ScheduleReconnect();

    /** Callback de una sola ejecución que inicia el siguiente intento. */
    bool HandleReconnectTimer(float DeltaTime);

    /** Invalida una conexión fallida y programa su reemplazo. */
    void HandleConnectionFailure(uint64 FailedGeneration, const FString& Reason);

    /** Confirma una conexión exitosa si pertenece a la generación actual. */
    void HandleSocketConnected(uint64 ConnectedGeneration);

    /** Comprueba si un callback pertenece al socket vigente. */
    bool IsCurrentConnection(uint64 CandidateGeneration) const;

    /**
     * Procesa un mensaje JSON de Binance.
     * Se ejecuta exclusivamente en Game Thread.
     */
    void ProcessWebSocketMessage(const FString& MessageString);


    // ------------------------------------------------------------
    // SUPERVISIÓN DE CONEXIÓN
    // ------------------------------------------------------------

    /** Registra el ticker periódico del watchdog. */
    void SetupWatchdogTicker();

    /**
     * Comprueba el tiempo transcurrido desde el último mensaje.
     * También detecta intentos de conexión que no completan el handshake.
     */
    bool HandleWatchdogTicker(float DeltaTime);


    // ------------------------------------------------------------
    // DISTRIBUCIÓN PERIÓDICA
    // ------------------------------------------------------------

    /** Registra el callback periódico de distribución de datos. */
    void SetupTicker();

    /**
     * Comprueba si hay datos nuevos y emite el delegado.
     * @return true para mantener el ticker registrado.
     */
    bool HandleTicker(float DeltaTime);


    // ------------------------------------------------------------
    // CONSTRUCCIÓN DE CONSULTAS ORDENADAS
    // ------------------------------------------------------------

    /**
     * Construye una instantánea siguiendo TargetSymbols.
     * El llamador debe mantener Mutex adquirido.
     */
    void BuildOrderedSnapshot(
        TArray<FCryptoAssetData>& OutAssets,
        bool bIncludePlaceholders
    ) const;


    // ------------------------------------------------------------
    // RECURSOS DE RED
    // ------------------------------------------------------------

    /** Cliente compartido de WebSocket utilizado para Binance. */
    TSharedPtr<IWebSocket> WebSocket;


    // ------------------------------------------------------------
    // ALMACENAMIENTO Y CATÁLOGO
    // ------------------------------------------------------------

    /** Caché principal: símbolo -> datos actuales. */
    TMap<FString, FCryptoAssetData> AssetCache;

    /** Filtro de símbolos permitidos, construido durante Initialize(). */
    TSet<FString> AllowedSymbols;


    // ------------------------------------------------------------
    // SINCRONIZACIÓN
    // ------------------------------------------------------------

    /** Protege AssetCache y bNewDataReceived. */
    FCriticalSection Mutex;


    // ------------------------------------------------------------
    // TICKERS Y ESTADO DEL SUBSISTEMA
    // ------------------------------------------------------------

    /** Handle del ticker que distribuye datos. */
    FTSTicker::FDelegateHandle TickerHandle;

    /** Handle del ticker que programa reconexiones. */
    FTSTicker::FDelegateHandle ReconnectTickerHandle;

    /** Handle del ticker que supervisa la conexión. */
    FTSTicker::FDelegateHandle WatchdogTickerHandle;

    /** Indica que hay datos nuevos pendientes de distribuir. */
    bool bNewDataReceived = false;

    /** Impide iniciar conexiones o procesar tareas durante el cierre. */
    bool bIsShuttingDown = false;
};