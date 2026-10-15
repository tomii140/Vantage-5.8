#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "IWebSocket.h"
#include "Containers/Queue.h"

class FCryptoWorker : public FRunnable
{
public:
    FCryptoWorker();
    virtual ~FCryptoWorker() override;

    virtual bool Init() override;
    virtual uint32 Run() override;
    virtual void Stop() override;

    void EnsureCompletion();

    // Cola thread-safe para extraer los mensajes recibidos desde el hilo principal
    bool GetPendingData(FString& OutMessage);

private:
    FRunnableThread* Thread;
    FThreadSafeCounter StopTaskCounter;

    TSharedPtr<IWebSocket> WebSocket;
    TQueue<FString, EQueueMode::Mpsc> MessageQueue; // Multi-producer, single-consumer queue

    void ConnectWebSocket();
};