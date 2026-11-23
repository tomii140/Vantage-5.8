#include "CryptoFinanceSubsystem.h"
#include "WebSocketsModule.h"
#include "Containers/Ticker.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

void UCryptoFinanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Asegura que el módulo WebSockets esté cargado antes de crear el socket
    if (!FModuleManager::Get().IsModuleLoaded("WebSockets"))
    {
        FModuleManager::LoadModuleChecked<FWebSocketsModule>("WebSockets");
    }

    // Si TargetSymbols está vacío en código, cargamos una base de ejemplo de 25 pares USDT.
    // Si ya los configuraste desde el editor/Blueprint, se respeta tu lista.
    if (TargetSymbols.Num() == 0)
    {
        TargetSymbols = {
            TEXT("BTCUSDT"), TEXT("ETHUSDT"), TEXT("BNBUSDT"), TEXT("SOLUSDT"), TEXT("XRPUSDT"),
            TEXT("ADAUSDT"), TEXT("DOGEUSDT"), TEXT("AVAXUSDT"), TEXT("DOTUSDT"), TEXT("LINKUSDT"),
            TEXT("TRXUSDT"), TEXT("TONUSDT"), TEXT("SHIBUSDT"), TEXT("LTCUSDT"), TEXT("BCHUSDT"),
            TEXT("UNIUSDT"), TEXT("NEARUSDT"), TEXT("APTUSDT"), TEXT("ATOMUSDT"), TEXT("ETCUSDT"),
            TEXT("FILUSDT"), TEXT("ICPUSDT"), TEXT("ARBUSDT"), TEXT("OPUSDT"), TEXT("SUIUSDT")
        };
    }

    // Normaliza los símbolos a mayúsculas para evitar fallos por minúsculas/espacios
    for (FString& Symbol : TargetSymbols)
    {
        Symbol = Symbol.ToUpper().TrimStartAndEnd();
    }

    // Clampea UpdateInterval al rango de rendimiento buscado (200 ms - 300 ms -> 0.2f - 0.3f s)
    UpdateInterval = FMath::Clamp(UpdateInterval, 0.2f, 0.3f);

    ConnectWebSocket();
    SetupTicker();
}

void UCryptoFinanceSubsystem::Deinitialize()
{
    // Elimina el ticker al destruir el GameInstance para evitar llamadas a objeto inválido
    if (TickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
        TickerHandle.Reset();
    }

    // Cierra limpiamente el WebSocket si está activo
    if (WebSocket.IsValid() && WebSocket->IsConnected())
    {
        WebSocket->Close();
    }

    WebSocket.Reset();

    Super::Deinitialize();
}

void UCryptoFinanceSubsystem::SetupTicker()
{
    if (TickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
        TickerHandle.Reset();
    }

    const float ClampedInterval = FMath::Clamp(UpdateInterval, 0.2f, 0.3f);

    // FTSTicker ejecuta HandleTicker cada ClampedInterval segundos (ej. 0.25s = 250 ms)
    // sin depender de un Actor en el nivel ni de cada Tick de renderizado.
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UCryptoFinanceSubsystem::HandleTicker),
        ClampedInterval
    );
}

void UCryptoFinanceSubsystem::ConnectWebSocket()
{
    // Stream masivo de Binance que envía actualizaciones del mercado en formato arreglo JSON
    const FString ServerUrl = TEXT("wss://stream.binance.com:9443/ws/!miniTicker@arr");

    WebSocket = FWebSocketsModule::Get().CreateWebSocket(ServerUrl);

    WebSocket->OnConnected().AddLambda([]()
        {
            UE_LOG(LogTemp, Log, TEXT("[Vantage] Conectado al WebSocket de Binance (!miniTicker@arr)."));
        });

    WebSocket->OnConnectionError().AddLambda([](const FString& Error)
        {
            UE_LOG(LogTemp, Error, TEXT("[Vantage] Error de conexión WebSocket: %s"), *Error);
        });

    WebSocket->OnClosed().AddLambda([](int32 StatusCode, const FString& Reason, bool bWasClean)
        {
            UE_LOG(LogTemp, Warning, TEXT("[Vantage] WebSocket cerrado (%d). Razón: %s"), StatusCode, *Reason);
        });

    // Este callback puede ejecutarse en un hilo distinto al GameThread
    WebSocket->OnMessage().AddLambda([this](const FString& MessageString)
        {
            TArray<TSharedPtr<FJsonValue>> JsonArray;
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(MessageString);

            if (!FJsonSerializer::Deserialize(Reader, JsonArray))
            {
                return;
            }

            // Construimos un TSet rápido con los símbolos objetivo para filtrar solo los que interesan
            TSet<FString> AllowedSet;
            {
                FScopeLock Lock(&Mutex);
                for (const FString& Target : TargetSymbols)
                {
                    if (!Target.IsEmpty())
                    {
                        AllowedSet.Add(Target);
                    }
                }
            }

            bool bUpdatedAny = false;

            // Bloqueamos Mutex solo mientras escribimos en AssetCache y marcamos la bandera
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

                const FString Symbol = JsonObject->GetStringField(TEXT("s")).ToUpper();

                // Si hay símbolos configurados en TargetSymbols, ignoramos el resto del mercado
                if (AllowedSet.Num() > 0 && !AllowedSet.Contains(Symbol))
                {
                    continue;
                }

                const double LastPrice = FCString::Atod(*JsonObject->GetStringField(TEXT("c")));
                const double OpenPrice = FCString::Atod(*JsonObject->GetStringField(TEXT("o")));
                const double HighPrice = FCString::Atod(*JsonObject->GetStringField(TEXT("h")));
                const double LowPrice = FCString::Atod(*JsonObject->GetStringField(TEXT("l")));
                const double Volume = FCString::Atod(*JsonObject->GetStringField(TEXT("v")));

                FCryptoAssetData AssetData;
                AssetData.Symbol = Symbol;
                AssetData.LastPrice = LastPrice;
                AssetData.HighPrice = HighPrice;
                AssetData.LowPrice = LowPrice;
                AssetData.Volume = Volume;

                // Calculamos variación absoluta y porcentual respecto al precio de apertura ("o")
                AssetData.PriceChange = LastPrice - OpenPrice;
                AssetData.PriceChangePercent = (OpenPrice > 0.0)
                    ? ((LastPrice - OpenPrice) / OpenPrice) * 100.0
                    : 0.0;

                AssetCache.Add(Symbol, AssetData);
                bUpdatedAny = true;
            }

            if (bUpdatedAny)
            {
                bNewDataReceived = true;
            }
        });

    WebSocket->Connect();
}

bool UCryptoFinanceSubsystem::HandleTicker(float DeltaTime)
{
    bool bShouldBroadcast = false;
    TArray<FCryptoAssetData> SnapshotList;

    {
        FScopeLock Lock(&Mutex);
        if (bNewDataReceived)
        {
            bNewDataReceived = false;
            bShouldBroadcast = true;
            AssetCache.GenerateValueArray(SnapshotList);
        }
    }

    // Se emite en el GameThread únicamente cuando llegaron datos nuevos y pasó UpdateInterval (200-300 ms)
    if (bShouldBroadcast)
    {
        OnCryptoDataUpdated.Broadcast(SnapshotList);
    }

    return true;
}

bool UCryptoFinanceSubsystem::GetAssetData(const FString& Symbol, FCryptoAssetData& OutData)
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

void UCryptoFinanceSubsystem::GetAllAssets(TArray<FCryptoAssetData>& OutAssets)
{
    OutAssets.Reset();
    FScopeLock Lock(&Mutex);
    AssetCache.GenerateValueArray(OutAssets);
}

void UCryptoFinanceSubsystem::GetOrderedTargetAssets(TArray<FCryptoAssetData>& OutOrderedAssets)
{
    OutOrderedAssets.Reset();

    FScopeLock Lock(&Mutex);
    OutOrderedAssets.Reserve(TargetSymbols.Num());

    for (const FString& RawSymbol : TargetSymbols)
    {
        const FString Symbol = RawSymbol.ToUpper().TrimStartAndEnd();
        if (Symbol.IsEmpty())
        {
            continue;
        }

        if (const FCryptoAssetData* Found = AssetCache.Find(Symbol))
        {
            OutOrderedAssets.Add(*Found);
        }
        else
        {
            // Mantiene el hueco en el orden de las 25 cryptos aunque aún no haya llegado su mensaje
            FCryptoAssetData Placeholder;
            Placeholder.Symbol = Symbol;
            OutOrderedAssets.Add(Placeholder);
        }
    }
}