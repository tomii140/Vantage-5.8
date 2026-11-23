#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "IWebSocket.h"
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
 * No se mantiene una segunda colección de FCryptoAssetData ordenada: duplicar
 * los registros exigiría sincronizar dos fuentes de datos. El orden se obtiene
 * recorriendo TargetSymbols y consultando AssetCache.
 *
 * Los mensajes de red se transfieren al Game Thread antes de tocar el estado
 * del subsistema. Mutex se conserva como protección del caché y de la bandera
 * de actualización frente a futuras rutas de acceso desde otros hilos.
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

    /** Detiene el ticker y cierra/libera la conexión antes de destruirse. */
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
     *
     * La salida respeta el orden de TargetSymbols. No incluye placeholders
     * para símbolos que todavía no recibieron datos.
     */
    UFUNCTION(BlueprintCallable, Category = "Vantage|Finance")
    void GetAllAssets(TArray<FCryptoAssetData>& OutAssets);

    /**
     * Devuelve todos los símbolos configurados en el orden de TargetSymbols.
     *
     * Si un activo todavía no llegó desde Binance, devuelve una estructura
     * provisional con el símbolo y sus valores numéricos en cero.
     */
    UFUNCTION(BlueprintCallable, Category = "Vantage|Finance")
    void GetOrderedTargetAssets(TArray<FCryptoAssetData>& OutOrderedAssets);


private:

    // ------------------------------------------------------------
    // CONEXIÓN Y PROCESAMIENTO DE RED
    // ------------------------------------------------------------

    /** Crea el cliente WebSocket y registra sus callbacks. */
    void ConnectWebSocket();

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
     *
     * IMPORTANTE: el llamador debe mantener Mutex adquirido durante la llamada.
     * Si bIncludePlaceholders es false, se omiten símbolos sin datos recibidos.
     * Si es true, se agrega una estructura provisional para cada símbolo faltante.
     *
     * Esta función es privada para centralizar la política de orden y evitar
     * implementar recorridos diferentes en el ticker y en las consultas.
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

    /**
     * Caché principal: símbolo -> datos actuales.
     *
     * TMap se conserva porque permite búsquedas rápidas por símbolo. No se usa
     * su orden interno para la presentación; el orden visible proviene de
     * TargetSymbols.
     */
    TMap<FString, FCryptoAssetData> AssetCache;

    /**
     * Conjunto de símbolos aceptados por el filtro de entrada.
     *
     * Se construye una sola vez a partir del catálogo normalizado. No se
     * reconstruye por mensaje. Se considera de solo lectura después de
     * Initialize(); los cambios dinámicos del catálogo requerirían una API
     * específica que actualice ambos contenedores de forma controlada.
     */
    TSet<FString> AllowedSymbols;


    // ------------------------------------------------------------
    // SINCRONIZACIÓN
    // ------------------------------------------------------------

    /**
     * Protege AssetCache y bNewDataReceived.
     * FScopeLock garantiza que el bloqueo se libere al salir del ámbito.
     */
    FCriticalSection Mutex;


    // ------------------------------------------------------------
    // TICKER Y ESTADO DEL SUBSISTEMA
    // ------------------------------------------------------------

    /** Handle necesario para retirar el callback registrado en FTSTicker. */
    FTSTicker::FDelegateHandle TickerHandle;

    /** Indica que hay datos nuevos pendientes de distribuir. */
    bool bNewDataReceived = false;

    /**
     * Se activa al comenzar Deinitialize().
     * Las tareas que todavía estén encoladas en el Game Thread descartan
     * el mensaje cuando detectan esta bandera.
     */
    bool bIsShuttingDown = false;
};
