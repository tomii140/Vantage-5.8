# Vantage

1. Estructura de Archivos y Carpetas Creadas
Source/Vantage/Public/ y Private/: Separación estándar de Unreal Engine entre archivos de cabecera (.h) y de implementación (.cpp).

CryptoWorker.h / .cpp: Define el hilo secundario asíncrono (FRunnable) encargado de manejar la conexión de red por WebSocket con Binance sin bloquear el motor de juego.

CryptoFinanceSubsystem.h / .cpp: Un subsistema de tipo GameInstanceSubsystem que persiste durante la ejecución global, administra el caché de activos financieros con seguridad de hilos (FCriticalSection) y se comunica mediante delegados.

CryptoInteractableInterface.h / .cpp: Interfaz universal expuesta a C++ y Blueprints para elementos interactivos en el entorno (paneles, carruseles 3D).

2. Qué hace cada componente (Implementación técnica realizada)
Concurrencia Segura (FCryptoWorker):

Establece la conexión WebSocket a la API de Binance (wss://[stream.binance.com:9443/ws/btcusdt@ticker](https://stream.binance.com:9443/ws/btcusdt@ticker)).

Utiliza una cola segura para hilos (TQueue) para evitar que el hilo de red escriba directamente en la memoria del motor (lo que causaría un cierre forzoso/crash).

Sincronización con el Hilo Principal (UCryptoFinanceSubsystem):

Registra un FTSTicker que revisa periódicamente la cola de mensajes en cada ciclo de actualización del juego de manera limpia.

Define la estructura de datos FCryptoAssetData lista para almacenar precios, cambios porcentuales y volúmenes.

Compatibilidad de Tipos corregida:

Se ajustó el manejo del identificador del ticker a FTSTicker::FDelegateHandle acorde a los requerimientos de la versión actual del motor.

3. Estado Actual
El proyecto compila exitosamente (DebugGame_Editor x64), con el enlace de WebSockets operativo y el puente seguro entre el hilo de red y el subsistema de finanzas listo. Mañana continuaremos con la deserialización del JSON para poblar el caché de datos en tiempo real.
