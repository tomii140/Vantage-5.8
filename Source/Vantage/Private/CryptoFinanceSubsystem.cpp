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
    // Stream masivo de Binance que transmite todo el mercado en un solo paquete JSON
    FString ServerUrl = TEXT("wss://stream.binance.com:9443/ws/!miniTicker@arr");

    WebSocket = FWebSocketsModule::Get().CreateWebSocket(ServerUrl);

    WebSocket->OnConnected().AddLambda([]()
        {
            UE_LOG(LogTemp, Log, TEXT("[Vantage] Conectado al WebSocket masivo de Binance (!miniTicker@arr)."));
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
            TArray<TSharedPtr<FJsonValue>> JsonArray;
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(MessageString);

            // El stream masivo llega como un arreglo JSON de objetos
            if (FJsonSerializer::Deserialize(Reader, JsonArray))
            {
                FScopeLock Lock(&Mutex);
                for (const auto& Value : JsonArray)
                {
                    TSharedPtr<FJsonObject> JsonObject = Value->AsObject();
                    if (JsonObject.IsValid())
                    {
                        FCryptoAssetData AssetData;
                        AssetData.Symbol = JsonObject->GetStringField(TEXT("s")).ToUpper();
                        AssetData.LastPrice = FCString::Atod(*JsonObject->GetStringField(TEXT("c")));
                        AssetData.HighPrice = FCString::Atod(*JsonObject->GetStringField(TEXT("h")));
                        AssetData.LowPrice = FCString::Atod(*JsonObject->GetStringField(TEXT("l")));
                        AssetData.Volume = FCString::Atod(*JsonObject->GetStringField(TEXT("v")));
                        AssetData.PriceChange = 0.0;
                        AssetData.PriceChangePercent = 0.0;

                        // Almacenamos/actualizamos cada activo en el mapa global
                        AssetCache.Add(AssetData.Symbol, AssetData);
                    }
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

void UCryptoFinanceSubsystem::GetAllAssets(TArray<FCryptoAssetData>& OutAssets)
{
    FScopeLock Lock(&Mutex);
    AssetCache.GenerateValueArray(OutAssets);
}