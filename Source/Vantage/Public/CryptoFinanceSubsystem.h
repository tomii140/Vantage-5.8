#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "IWebSocket.h"
#include "CryptoFinanceSubsystem.generated.h"

/**
 * Estructura con los datos de mercado de una criptomoneda.
 * Se inicializan todos los valores numéricos en 0.0 para evitar basura en memoria
 * antes de recibir el primer paquete del WebSocket.
 */
USTRUCT(BlueprintType)
struct FCryptoAssetData
{
    GENERATED_BODY()

    // Símbolo del par en mayúsculas (ej. "BTCUSDT")
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    FString Symbol;

    // Último precio / precio de cierre actual ("c" en Binance miniTicker)
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double LastPrice = 0.0;

    // Variación absoluta respecto al precio de apertura ("c" - "o")
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double PriceChange = 0.0;

    // Variación porcentual respecto al precio de apertura ((LastPrice - OpenPrice) / OpenPrice) * 100
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double PriceChangePercent = 0.0;

    // Precio máximo del período ("h")
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double HighPrice = 0.0;

    // Precio mínimo del período ("l")
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double LowPrice = 0.0;

    // Volumen operado ("v")
    UPROPERTY(BlueprintReadWrite, Category = "Vantage|Finance")
    double Volume = 0.0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCryptoDataUpdated, const TArray<FCryptoAssetData>&, UpdatedAssets);

/**
 * Subsistema global (vive durante toda la sesión del GameInstance) encargado de:
 * - Mantener la conexión WebSocket con Binance.
 * - Almacenar en caché los activos de forma thread-safe.
 * - Emitir actualizaciones periódicas (200-300 ms) sin usar Tick por frame.
 */
UCLASS()
class VANTAGE_API UCryptoFinanceSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // Evento emitido cuando llegan datos nuevos y vence el intervalo configurado (200-300 ms)
    UPROPERTY(BlueprintAssignable, Category = "Vantage|Events")
    FOnCryptoDataUpdated OnCryptoDataUpdated;

    /**
     * Lista ordenada de símbolos objetivo (ej. 25 criptomonedas).
     * Se expone en Detalles para definir qué activos priorizar y en qué orden mostrarlos.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Catalog")
    TArray<FString> TargetSymbols;

    /**
     * Intervalo de refresco en segundos para el ticker de datos (rango objetivo: 0.2f a 0.3f s).
     * Evita saturar el GameThread en cada frame.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Performance", meta = (ClampMin = "0.1", ClampMax = "2.0"))
    float UpdateInterval = 0.25f;

    // Obtiene los datos de un símbolo específico de forma thread-safe
    UFUNCTION(BlueprintCallable, Category = "Vantage|Finance")
    bool GetAssetData(const FString& Symbol, FCryptoAssetData& OutData);

    // Devuelve todos los activos cacheados actualmente (sin orden garantizado)
    UFUNCTION(BlueprintCallable, Category = "Vantage|Finance")
    void GetAllAssets(TArray<FCryptoAssetData>& OutAssets);

    /**
     * Devuelve los activos en el orden exacto de TargetSymbols.
     * Si un símbolo aún no llegó por WebSocket, devuelve una entrada por defecto con su Symbol
     * para mantener índices estables (útil para paginar de 5 en 5 sobre 25 criptos).
     */
    UFUNCTION(BlueprintCallable, Category = "Vantage|Finance")
    void GetOrderedTargetAssets(TArray<FCryptoAssetData>& OutOrderedAssets);

private:
    // Crea y conecta el cliente WebSocket al stream de Binance
    void ConnectWebSocket();

    // Registra o reconfigura el ticker periódico según UpdateInterval
    void SetupTicker();

    // Callback ejecutado en el CoreTicker según UpdateInterval
    bool HandleTicker(float DeltaTime);

    TSharedPtr<IWebSocket> WebSocket;

    // Caché en memoria clave-valor: Símbolo en mayúsculas -> Datos del activo
    TMap<FString, FCryptoAssetData> AssetCache;

    // Mutex para proteger AssetCache y bNewDataReceived entre hilo de red y GameThread
    FCriticalSection Mutex;

    // Handle del ticker registrado en FTSTicker
    FTSTicker::FDelegateHandle TickerHandle;

    // Bandera que indica si llegó un paquete JSON nuevo desde la última emisión
    bool bNewDataReceived = false;
};