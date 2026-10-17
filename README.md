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

Aquí tienes la actualización completa del registro técnico basada en lo que realmente implementamos y corregimos hasta este momento, lista para tu commit:

# Vantage - Actualización del Estado del Proyecto 15/10/2026

1. Modificaciones y Evolución de la Arquitectura
Limpieza de Hilos Secundarios (FCryptoWorker / FRunnable): Se eliminaron por completo los archivos físicos y lógicos de CryptoWorker.h y CryptoWorker.cpp, trasladando la conexión de red WebSocket de forma directa y nativa al hilo principal dentro del subsistema para mayor estabilidad.

Subsistema Global (UCryptoFinanceSubsystem): Mantiene la persistencia a nivel de GameInstance, gestionando el caché de activos con seguridad de hilos mediante FCriticalSection y emitiendo actualizaciones fluidas hacia los actores visuales.

Stream de Binance Optimizado: Se migró con éxito la URL del WebSocket del endpoint completo (@ticker) al endpoint @miniTicker (wss://[stream.binance.com:9443/ws/btcusdt@miniTicker](https://stream.binance.com:9443/ws/btcusdt@miniTicker)), eliminando campos ambiguos y marcas de tiempo que desbordaban los valores numéricos.

Tipos de Datos de Alta Precisión: Se actualizaron todas las variables de la estructura FCryptoAssetData a tipo double y se implementó la función FCString::Atod para garantizar una conversión numérica exacta y sin distorsiones de los precios en tiempo real.

Throttling y Sincronización: Se configuró un FTSTicker con un intervalo de control de 300 ms (0.3s) para regular el broadcast visual hacia el ACryptoTerminalActor y evitar saturación en pantalla.

2. Archivos Afectados
Eliminados: CryptoWorker.h, CryptoWorker.cpp.

Actualizados:

CryptoFinanceSubsystem.h y .cpp (Lógica de red nativa, stream miniTicker y precisión double/Atod).

ACryptoTerminalActor.cpp (Lectura robusta desde el caché del subsistema y formato de impresión en consola/pantalla).

3. Estado Actual
El proyecto compila exitosamente (Succeeded en Development_Editor x64), la conexión WebSocket con Binance se establece de forma limpia y la terminal del motor (ACryptoTerminalActor) ya imprime en tiempo real y con total precisión las cotizaciones reales de Bitcoin (BTCUSDT).
--------------------------------------------------------------------------------------------------------

# Vantage — Documentación del Estado del Proyecto y Registro de Cambios
Fecha: 15/10/2026

1. Evolución de la Arquitectura y Modificaciones de Código
Optimización de Concurrencia (Eliminación de hilos secundarios): Se removieron por completo los archivos CryptoWorker.h y CryptoWorker.cpp. La gestión de la conexión WebSocket se trasladó de forma directa y nativa al hilo principal dentro del subsistema para maximizar la estabilidad.

Subsistema Global (UCryptoFinanceSubsystem): Mantiene la persistencia a nivel de GameInstance, asegurando el caché de activos mediante exclusión mutua (FCriticalSection) y sincronizando las actualizaciones hacia los actores visuales.

Stream de Binance Optimizado: Migración del endpoint completo (@ticker) al endpoint ligero @miniTicker (wss://[stream.binance.com:9443/ws/btcusdt@miniTicker](https://stream.binance.com:9443/ws/btcusdt@miniTicker)), eliminando campos redundantes que saturaban el procesamiento numérico.

Tipos de Datos de Alta Precisión: Actualización de todas las variables en la estructura FCryptoAssetData a tipo double, implementando FCString::Atod para conversiones numéricas exactas sin pérdida de decimales.

Control de Frecuencia (Throttling): Configuración de un FTSTicker con un intervalo fijo de 300 ms (0.3 s) para regular el flujo de emisiones visuales hacia el terminal y evitar sobrecarga en pantalla.

2. Inventario de Archivos Afectados
Eliminados:

CryptoWorker.h

CryptoWorker.cpp

Actualizados:

CryptoFinanceSubsystem.h y .cpp (Lógica de red nativa y stream miniTicker).

ACryptoTerminalActor.cpp (Lectura robusta desde el subsistema).

No Actualizados / Descartados:

Módulos y plugins de OpenXR (descartados al no requerir cascos físicos de RV/RA; el desarrollo se enfoca en despliegue móvil táctil con simulación espacial nativa).

3. Estado Actual del Proyecto
Compilación: Exitosa (Succeeded en configuración Development_Editor x64).

Conectividad: Enlace WebSocket con Binance operativo y estable, procesando cotizaciones en tiempo real (ej. BTCUSDT).

Arquitectura de Interfaz Definida:

Estructura híbrida 2D/3D para smartphones.

Visualización minimalista por defecto (solo nombres/símbolos de criptomonedas).

Sistema de layouts modulares (moldes geométricos 3D: diagonal, horizontal, vertical, circular; vistas 2D: fila de 5 y matriz extendida de 15 elementos).

Interfaz de detalle bajo demanda mediante toque (tap) para desplegar la información completa del subsistema.

# 
----------------------------------------------------------------------------------------------------
Resumen: Refactorización arquitectónica del sistema de visualización de criptomonedas, eliminando los actores individuales 3D en favor de un sistema centralizado de texturas dinámicas sobre un plano estandarizado.

Problemas y Soluciones
Problema: Discrepancias de escala, rotación y pivotes entre los distintos modelos FBX de las criptomonedas, haciendo inviable el mantenimiento de múltiples Blueprints individuales.

Solución: Eliminación completa de la clase base ACryptoCoinBase y sus derivados. Se centralizó la representación visual en ACryptoSlotActor utilizando un plano genérico con materiales dinámicos (UMaterialInstanceDynamic) para proyectar texturas estandarizadas.

Problema: Errores de compilación por referencias huérfanas en ACryptoTerminalActor hacia la clase eliminada.

Solución: Refactorización del terminal para reemplazar CoinClassMap por CoinTextureMap (TMap<FString, UTexture2D*>).

Problema: Restricciones de transformación y rotación en el visor de Blueprints debido a que la malla estática actuaba como componente raíz (RootComponent).

Solución: Establecimiento de un USceneComponent genérico como raíz (SceneRoot) y reestructuración de la jerarquía de componentes hijos.

Problema: Errores de sintaxis del Unreal Header Tool (UHT) por orden incorrecto de includes y nombres de archivo desalineados.

Solución: Reordenamiento de las macros de generación (.generated.h al final) y unificación de nomenclaturas en los archivos fuente.

Pendientes
Asignar las texturas de las criptomonedas en el CoinTextureMap desde el editor en el Blueprint del terminal.

Completar la lógica de asignación de textura en el método InitSlot de ACryptoSlotActor.