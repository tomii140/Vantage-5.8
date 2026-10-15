#include "CryptoWorker.h"
#include "WebSocketsModule.h"
#include "IWebSocket.h"

FCryptoWorker::FCryptoWorker()
    : StopTaskCounter(0)
{
    Thread = FRunnableThread::Create(this, TEXT("CryptoWorkerThread"), 0, TPri_Normal);
}

FCryptoWorker::~FCryptoWorker()
{
    EnsureCompletion();
    delete Thread;
    Thread = nullptr;
}

bool FCryptoWorker::Init()
{
    if (!FModuleManager::Get().IsModuleLoaded("WebSockets"))
    {
        FModuleManager::LoadModuleChecked<FWebSocketsModule>("WebSockets");
    }

    ConnectWebSocket();
    return true;
}

uint32 FCryptoWorker::Run()
{
    while (StopTaskCounter.GetValue() == 0)
    {
        FPlatformProcess::Sleep(0.016f); // ~60 FPS de polling
    }

    if (WebSocket.IsValid() && WebSocket->IsConnected())
    {
        WebSocket->Close();
    }

    return 0;
}

void FCryptoWorker::Stop()
{
    StopTaskCounter.Increment();
}

void FCryptoWorker::EnsureCompletion()
{
    Stop();
    if (Thread)
    {
        Thread->WaitForCompletion();
    }
}

bool FCryptoWorker::GetPendingData(FString& OutMessage)
{
    return MessageQueue.Dequeue(OutMessage);
}

void FCryptoWorker::ConnectWebSocket()
{
    FString ServerUrl = TEXT("wss://stream.binance.com:9443/ws/btcusdt@ticker");

    WebSocket = FWebSocketsModule::Get().CreateWebSocket(ServerUrl);

    WebSocket->OnConnected().AddLambda([]()
        {
            UE_LOG(LogTemp, Log, TEXT("[Vantage] Conectado exitosamente al WebSocket de Binance."));
        });

    WebSocket->OnConnectionError().AddLambda([](const FString& Error)
        {
            UE_LOG(LogTemp, Error, TEXT("[Vantage] Error de conexión WebSocket: %s"), *Error);
        });

    WebSocket->OnClosed().AddLambda([](int32 StatusCode, const FString& Reason, bool bWasClean)
        {
            UE_LOG(LogTemp, Warning, TEXT("[Vantage] WebSocket cerrado. Razón: %s"), *Reason);
        });

    // Guardamos una referencia débil o capturamos por puntero seguro si fuera necesario, 
    // pero aquí encolamos el mensaje de forma segura para sacarlo en el hilo principal.
    WebSocket->OnMessage().AddLambda([this](const FString& MessageString)
        {
            MessageQueue.Enqueue(MessageString);
        });

    WebSocket->Connect();
}