#include "CryptoFinanceSubsystem.h"
#include "WebSocketsModule.h"
#include "Containers/Ticker.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

void UCryptoFinanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    if (!FModuleManager::Get().IsModuleLoaded("WebSockets"))
    {
        FModuleManager::LoadModuleChecked<FWebSocketsModule>("WebSockets");
    }

    ConnectWebSocket();

    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UCryptoFinanceSubsystem::HandleTicker), 0.3f
    );
}

void UCryptoFinanceSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);

    if (WebSocket.IsValid() && WebSocket->IsConnected())
    {
        WebSocket->Close();
    }

    Super::Deinitialize();
}

void UCryptoFinanceSubsystem::ConnectWebSocket()
{
    // 1. Cambiado a @miniTicker para evitar campos ambiguos de timestamps
    FString ServerUrl = TEXT("wss://stream.binance.com:9443/ws/btcusdt@miniTicker");

    WebSocket = FWebSocketsModule::Get().CreateWebSocket(ServerUrl);

    WebSocket->OnConnected().AddLambda([]()
        {
            UE_LOG(LogTemp, Log, TEXT("[Vantage] Conectado exitosamente al WebSocket de Binance (miniTicker)."));
        });

    WebSocket->OnConnectionError().AddLambda([](const FString& Error)
        {
            UE_LOG(LogTemp, Error, TEXT("[Vantage] Error de conexión WebSocket: %s"), *Error);
        });

    WebSocket->OnClosed().AddLambda([](int32 StatusCode, const FString& Reason, bool bWasClean)
        {
            UE_LOG(LogTemp, Warning, TEXT("[Vantage] WebSocket cerrado. Razón: %s"), *Reason);
        });

    WebSocket->OnMessage().AddLambda([this](const FString& MessageString)
        {
            TSharedPtr<FJsonObject> JsonObject;
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(MessageString);

            if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
            {
                FCryptoAssetData AssetData;

                AssetData.Symbol = JsonObject->GetStringField(TEXT("s")).ToUpper();

                // 2. Uso de Atod para precisión exacta (double) evitando desbordamientos
                AssetData.LastPrice = FCString::Atod(*JsonObject->GetStringField(TEXT("c")));
                AssetData.HighPrice = FCString::Atod(*JsonObject->GetStringField(TEXT("h")));
                AssetData.LowPrice = FCString::Atod(*JsonObject->GetStringField(TEXT("l")));
                AssetData.Volume = FCString::Atod(*JsonObject->GetStringField(TEXT("v")));

                // El miniTicker no envía variaciones porcentuales directamente, se inicializan seguras
                AssetData.PriceChange = 0.0;
                AssetData.PriceChangePercent = 0.0;

                {
                    FScopeLock Lock(&Mutex);
                    AssetCache.Add(AssetData.Symbol, AssetData);
                }

                bNewDataReceived = true;
            }
        });

    WebSocket->Connect();
}

bool UCryptoFinanceSubsystem::HandleTicker(float DeltaTime)
{
    if (bNewDataReceived)
    {
        bNewDataReceived = false;

        TArray<FCryptoAssetData> CachedList;
        {
            FScopeLock Lock(&Mutex);
            AssetCache.GenerateValueArray(CachedList);
        }

        OnCryptoDataUpdated.Broadcast(CachedList);
    }

    return true;
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