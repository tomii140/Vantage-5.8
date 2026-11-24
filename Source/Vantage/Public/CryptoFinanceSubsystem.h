
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "IWebSocket.h"
#include "Containers/Ticker.h"
#include "CryptoFinanceSubsystem.generated.h"

/**
 * FCryptoAssetData
 *
 * Contiene la última información de mercado conocida para un activo.
 * Binance identifica cada mercado mediante un símbolo, por ejemplo BTCUSDT.
 *
 * Los valores numéricos comienzan en cero. Esto permite crear estructuras
 * provisionales para activos que todavía no recibieron su primer mensaje.
 */
USTRUCT(BlueprintType)
struct FCryptoAssetData
{
    GENERATED_BODY()

    /** Identificador del mercado normalizado en mayúsculas. */
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    FString Symbol;

    /** Último precio informado por Binance (campo JSON "c"). */
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double LastPrice = 0.0;

    /** Variación absoluta respecto del precio de apertura (c - o). */
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double PriceChange = 0.0;

    /** Variación porcentual respecto del precio de apertura. */
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double PriceChangePercent = 0.0;

    /** Precio máximo del período informado (campo JSON "h"). */
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double HighPrice = 0.0;

    /** Precio mínimo del período informado (campo JSON "l"). */
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double LowPrice = 0.0;

    /** Volumen negociado del período informado (campo JSON "v"). */
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double Volume = 0.0;
};

/**
 * Evento emitido cuando hay información financiera nueva para distribuir.
 *
 * El array se entrega como referencia constante durante la ejecución del
 * delegado. Los consumidores que necesiten conservarlo deben hacer su propia
 * copia; no deben guardar una referencia al parámetro.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnCryptoDataUpdated,
    const TArray<FCryptoAssetData>&,
    UpdatedAssets
);

/**
 * UCryptoFinanceSubsystem
 *
 * Subsistema central de recepción, almacenamiento y distribución de datos
 * financieros. Su ciclo de vida está asociado al GameInstance.
 *
 * ARQUITECTURA DE DATOS:
 *
 * - TargetSymbols (TArray): catálogo ordenado y fuente de verdad para la UI.
 * - AllowedSymbols (TSet): filtro de pertenencia rápida, construido una vez.
 * - AssetCache (TMap): almacenamiento principal y búsqueda por símbolo.
 *
 * No se mantiene una segunda colección ordenada: el orden se obtiene
 * recorriendo TargetSymbols y consultando AssetCache.
 *
 * ARQUITECTURA DE CONEXIÓN:
 *
 * - Mantiene una sola conexión activa o en proceso de conexión.
 * - Reintenta automáticamente ante errores y cierres inesperados.
 * - Utiliza backoff exponencial con un máximo de 30 segundos.
 * - Asigna una generación a cada conexión para descartar callbacks antiguos.
 * - Cancela los reintentos durante Deinitialize().
 *
 * Los callbacks de red transfieren su procesamiento al Game Thread antes de
 * modificar el estado del UObject o del caché.
 */
UCLASS()
class VANTAGE_API UCryptoFinanceSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:

    // ------------------------------------------------------------
    // CICLO DE VIDA
    // ------------------------------------------------------------

    /** Inicializa el catálogo, el filtro, la conexión y el ticker. */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    /** Detiene los tickers y cierra/libera la conexión antes de destruirse. */
    virtual void Deinitialize() override;


    // ------------------------------------------------------------
    // EVENTOS PÚBLICOS
    // ------------------------------------------------------------

    /** Se emite cuando se detecta información nueva de los activos configurados. */
    UPROPERTY(BlueprintAssignable, Category = "Vantage|Events")
    FOnCryptoDataUpdated OnCryptoDataUpdated;


    // ------------------------------------------------------------
    // CONFIGURACIÓN PÚBLICA
    // ------------------------------------------------------------

    /**
     * Catálogo de activos que Vantage debe procesar y visualizar.
     *
     * El orden de esta lista determina el orden estable de las consultas
     * ordenadas y, por lo tanto, el orden utilizado para paginar los slots.
     * Los símbolos se normalizan y se eliminan duplicados durante Initialize().
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Catalog")
    TArray<FString> TargetSymbols;

    /**
     * Intervalo de distribución del delegado, en segundos.
     * No controla la frecuencia de mensajes enviada por Binance.
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
     * @param Symbol Identificador del mercado; se normaliza antes de buscar.
     * @param OutData Recibe una copia de los datos si el activo existe.
     * @return true si se encontró el activo; false en caso contrario.
     */
    UFUNCTION(BlueprintCallable, Category = "Vantage|Finance")
    bool GetAssetData(const FString& Symbol, FCryptoAssetData& OutData);

    /**
     * Devuelve los activos recibidos actualmente.
     * La salida respeta el orden de TargetSymbols y no incluye placeholders.
     */
    UFUNCTION(BlueprintCallable, Category = "Vantage|Finance")
    void GetAllAssets(TArray<FCryptoAssetData>& OutAssets);

    /**
     * Devuelve todos los símbolos configurados en el orden de TargetSymbols.
     * Los activos todavía no recibidos se representan mediante placeholders.
     */
    UFUNCTION(BlueprintCallable, Category = "Vantage|Finance")
    void GetOrderedTargetAssets(TArray<FCryptoAssetData>& OutOrderedAssets);


private:

    // ------------------------------------------------------------
    // ESTADO DE CONEXIÓN
    // ------------------------------------------------------------

    /**
     * Estados internos del ciclo de vida del WebSocket.
     * El estado se modifica exclusivamente desde el Game Thread.
     */
    enum class EConnectionState : uint8
    {
        Disconnected,
        Connecting,
        Connected,
        Reconnecting,
        ShuttingDown
    };

    /** Estado actual de la conexión. */
    EConnectionState ConnectionState = EConnectionState::Disconnected;

    /** Identificador incremental para invalidar callbacks de sockets anteriores. */
    uint64 ConnectionGeneration = 0;

    /** Cantidad de reintentos consecutivos desde la última conexión exitosa. */
    int32 ReconnectAttempt = 0;

    /** Espera inicial antes del primer reintento, expresada en segundos. */
    static constexpr float ReconnectBaseDelay = 1.0f;

    /** Espera máxima entre intentos consecutivos, expresada en segundos. */
    static constexpr float ReconnectMaxDelay = 30.0f;


    // ------------------------------------------------------------
    // CONEXIÓN Y PROCESAMIENTO DE RED
    // ------------------------------------------------------------

    /** Crea el cliente WebSocket y registra sus callbacks. */
    void ConnectWebSocket();

    /** Programa un único intento de reconexión con espera progresiva. */
    void ScheduleReconnect();

    /** Callback de una sola ejecución que inicia el siguiente intento. */
    bool HandleReconnectTimer(float DeltaTime);

    /** Invalida la conexión fallida y programa su reemplazo. */
    void HandleConnectionFailure(uint64 FailedGeneration, const FString& Reason);

    /** Confirma una conexión exitosa si pertenece a la generación actual. */
    void HandleSocketConnected(uint64 ConnectedGeneration);

    /** Comprueba si un callback todavía pertenece al socket vigente. */
    bool IsCurrentConnection(uint64 CandidateGeneration) const;

    /**
     * Procesa un mensaje JSON de Binance.
     * Esta función se ejecuta exclusivamente en el Game Thread.
     */
    void ProcessWebSocketMessage(const FString& MessageString);


    // ------------------------------------------------------------
    // DISTRIBUCIÓN PERIÓDICA
    // ------------------------------------------------------------

    /** Registra el callback periódico de distribución de datos. */
    void SetupTicker();

    /**
     * Comprueba si hay datos nuevos y emite el delegado cuando corresponde.
     * @return true para mantener el ticker registrado.
     */
    bool HandleTicker(float DeltaTime);


    // ------------------------------------------------------------
    // CONSTRUCCIÓN DE CONSULTAS ORDENADAS
    // ------------------------------------------------------------

    /**
     * Construye una instantánea siguiendo el orden de TargetSymbols.
     * El llamador debe mantener Mutex adquirido durante la llamada.
     */
    void BuildOrderedSnapshot(
        TArray<FCryptoAssetData>& OutAssets,
        bool bIncludePlaceholders
    ) const;


private:

    // ------------------------------------------------------------
    // RECURSOS DE RED
    // ------------------------------------------------------------

    /** Cliente compartido de WebSocket utilizado para comunicarse con Binance. */
    TSharedPtr<IWebSocket> WebSocket;


    // ------------------------------------------------------------
    // ALMACENAMIENTO Y CATÁLOGO
    // ------------------------------------------------------------

    /** Caché principal: símbolo -> datos actuales. */
    TMap<FString, FCryptoAssetData> AssetCache;

    /**
     * Conjunto de símbolos aceptados por el filtro de entrada.
     * Se construye una sola vez durante Initialize().
     */
    TSet<FString> AllowedSymbols;


    // ------------------------------------------------------------
    // SINCRONIZACIÓN
    // ------------------------------------------------------------

    /** Protege AssetCache y bNewDataReceived. */
    FCriticalSection Mutex;


    // ------------------------------------------------------------
    // TICKERS Y ESTADO DEL SUBSISTEMA
    // ------------------------------------------------------------

    /** Handle del callback periódico que distribuye datos a los consumidores. */
    FTSTicker::FDelegateHandle TickerHandle;

    /** Handle del callback de una sola ejecución para reintentar la conexión. */
    FTSTicker::FDelegateHandle ReconnectTickerHandle;

    /** Indica que hay datos nuevos pendientes de distribuir. */
    bool bNewDataReceived = false;

    /** Impide iniciar conexiones o procesar tareas después del cierre. */
    bool bIsShuttingDown = false;
};
