#include "CryptoFinanceSubsystem.h"
#include "Containers/Ticker.h"

void UCryptoFinanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Arrancamos el hilo secundario de red
    CryptoWorker = MakeShared<FCryptoWorker>();

    // Registramos un ticker que se ejecuta cada frame en el hilo principal de Unreal
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UCryptoFinanceSubsystem::Tick), 0.0f
    );
}

void UCryptoFinanceSubsystem::Deinitialize()
{
    // Desregistramos el ticker
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);

    // Detenemos y limpiamos el hilo de forma segura
    if (CryptoWorker.IsValid())
    {
        CryptoWorker->EnsureCompletion();
        CryptoWorker.Reset();
    }

    Super::Deinitialize();
}

bool UCryptoFinanceSubsystem::Tick(float DeltaTime)
{
    if (!CryptoWorker.IsValid())
    {
        return true; // Continuar intentando
    }

    FString RawJson;
    // Procesamos todos los mensajes acumulados en la cola de forma segura en el hilo principal
    while (CryptoWorker->GetPendingData(RawJson))
    {
        // Aquí procesarás el JSON de Binance en el siguiente paso. 
        // Por ahora dejamos constancia de que el dato llegó al hilo principal sin peligro.
        UE_LOG(LogTemp, Verbose, TEXT("[Vantage] Dato recibido en hilo principal: %s"), *RawJson);
    }

    return true; // Mantener activo el ticker
}

bool UCryptoFinanceSubsystem::GetAssetData(const FString& Symbol, FCryptoAssetData& OutData)
{
    FScopeLock Lock(&Mutex);
    if (AssetCache.Contains(Symbol))
    {
        OutData = AssetCache[Symbol];
        return true;
    }
    return false;
}