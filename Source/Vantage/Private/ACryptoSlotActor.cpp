#include "ACryptoSlotActor.h"

// Componentes.
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"

// Materiales.
#include "Materials/MaterialInstanceDynamic.h"

// Texturas.
#include "Engine/Texture2D.h"


// ============================================================
// CONSTRUCTOR
// ============================================================

/**
 * ACryptoSlotActor
 *
 * Construye la jerarquía visual del slot.
 *
 * ESTRUCTURA:
 *
 * RootComponent
 * └── CardMeshComponent
 *     ├── IconPlaneComponent
 *     └── UIWidgetComponent
 *
 * El plano contiene el logo y el Widget contiene los datos.
 */
ACryptoSlotActor::ACryptoSlotActor()
{
    // El slot recibe actualizaciones mediante InitSlot.
    // No necesita ejecutar lógica continuamente por Tick.
    PrimaryActorTick.bCanEverTick = false;

    // --------------------------------------------------------
    // ROOT COMPONENT
    // --------------------------------------------------------

    RootComponent = CreateDefaultSubobject<USceneComponent>(
        TEXT("RootComponent")
    );

    // --------------------------------------------------------
    // CARD MESH
    // --------------------------------------------------------

    CardMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(
        TEXT("CardMeshComponent")
    );

    CardMeshComponent->SetupAttachment(RootComponent);

    // --------------------------------------------------------
    // ICON PLANE
    // --------------------------------------------------------

    IconPlaneComponent = CreateDefaultSubobject<UStaticMeshComponent>(
        TEXT("IconPlaneComponent")
    );

    IconPlaneComponent->SetupAttachment(CardMeshComponent);

    // --------------------------------------------------------
    // WIDGET COMPONENT
    // --------------------------------------------------------

    UIWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(
        TEXT("UIWidgetComponent")
    );

    UIWidgetComponent->SetupAttachment(CardMeshComponent);

    // El Widget se representa físicamente dentro del mundo.
    UIWidgetComponent->SetWidgetSpace(EWidgetSpace::World);

    // Permite ajustar el tamaño según el contenido del Widget.
    UIWidgetComponent->SetDrawAtDesiredSize(true);
}


// ============================================================
// CICLO DE VIDA
// ============================================================

/**
 * BeginPlay
 *
 * Se ejecuta cuando el slot comienza a existir en el mundo.
 *
 * Crea el material dinámico y vuelve a aplicar la textura
 * almacenada por si InitSlot fue llamado antes de BeginPlay.
 */
void ACryptoSlotActor::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[Vantage][Slot][TRACE] BeginPlay | Actor: %s | IconPlane válido: %s | Materiales: %d | Símbolo ya inicializado: %s"),
        *GetName(),
        IsValid(IconPlaneComponent) ? TEXT("SI") : TEXT("NO"),
        IsValid(IconPlaneComponent) ? IconPlaneComponent->GetNumMaterials() : 0,
        bHasInitializedData ? *CurrentAssetData.Symbol : TEXT("NO")
    );

    // Crear la instancia dinámica del material.
    CreateDynamicIconMaterial();

    // Recuperar la textura si ya había sido recibida.
    if (bHasInitializedData)
    {
        ApplyIconTexture();
    }
    else
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[Vantage][Slot][TRACE] BeginPlay terminó sin datos previos. Actor: %s"),
            *GetName()
        );
    }
}


// ============================================================
// INICIALIZACIÓN DEL SLOT
// ============================================================

/**
 * InitSlot
 *
 * Recibe los datos financieros y la textura correspondiente.
 *
 * IMPORTANTE:
 * Cada instancia de ACryptoSlotActor mantiene su propia textura
 * y su propia instancia dinámica del material.
 *
 * Por lo tanto:
 * BTC puede mostrar Bitcoin.png mientras ETH muestra Ethereum.png,
 * sin modificar el material original compartido.
 */
void ACryptoSlotActor::InitSlot(
    const FCryptoAssetData& AssetData,
    UTexture2D* IconTexture
)
{
    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[Vantage][Slot][TRACE] InitSlot | Actor: %s | Símbolo: '%s' | Textura recibida: %s | MID existente: %s"),
        *GetName(),
        *AssetData.Symbol,
        *GetNameSafe(IconTexture),
        IsValid(DynamicIconMaterial) ? TEXT("SI") : TEXT("NO")
    );

    // --------------------------------------------------------
    // 1. GUARDAR LOS DATOS
    // --------------------------------------------------------

    CurrentAssetData = AssetData;

    // Guardar la referencia de la textura recibida.
    CurrentIconTexture = IconTexture;

    bHasInitializedData = true;

    // --------------------------------------------------------
    // 2. VALIDAR EL MATERIAL DINÁMICO
    // --------------------------------------------------------

    /*
     * Si BeginPlay ya ocurrió, normalmente el material existe.
     *
     * Si todavía no ocurrió, intentamos crearlo cuando el
     * componente y su material ya estén disponibles.
     */
    CreateDynamicIconMaterial();

    // --------------------------------------------------------
    // 3. APLICAR EL LOGO
    // --------------------------------------------------------

    ApplyIconTexture();

    // --------------------------------------------------------
    // 4. ACTUALIZAR BLUEPRINT
    // --------------------------------------------------------

    /*
     * Este evento permite actualizar los textos del Widget.
     * La lógica visual de los textos continúa en WBP_CryptoSlot.
     */
    BP_OnSlotDataUpdated(CurrentAssetData);

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[Vantage][Slot][TRACE] InitSlot finalizado | Actor: %s | Símbolo: '%s' | Textura guardada: %s | MID válido: %s"),
        *GetName(),
        *CurrentAssetData.Symbol,
        *GetNameSafe(CurrentIconTexture),
        IsValid(DynamicIconMaterial) ? TEXT("SI") : TEXT("NO")
    );
}


// ============================================================
// CONSULTA DE DATOS
// ============================================================

/**
 * GetCurrentAssetData
 *
 * Devuelve una copia de los datos actuales.
 *
 * No permite modificar directamente el estado interno del slot.
 */
FCryptoAssetData ACryptoSlotActor::GetCurrentAssetData() const
{
    return CurrentAssetData;
}


// ============================================================
// CREACIÓN DEL MATERIAL DINÁMICO
// ============================================================

/**
 * CreateDynamicIconMaterial
 *
 * Crea una instancia dinámica del material del IconPlaneComponent.
 *
 * ¿POR QUÉ UTILIZAMOS UN MATERIAL DINÁMICO?
 *
 * El material original puede estar compartido por los 25 slots.
 *
 * Si modificáramos directamente el material original,
 * podríamos terminar cambiando el logo de todos los slots.
 *
 * La instancia dinámica permite que cada slot tenga su propia
 * textura sin modificar los demás.
 */
void ACryptoSlotActor::CreateDynamicIconMaterial()
{
    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[Vantage][Slot][TRACE] CreateDynamicIconMaterial | Actor: %s | Componente: %s | Índice configurado: %d"),
        *GetName(),
        *GetNameSafe(IconPlaneComponent),
        IconMaterialIndex
    );

    // --------------------------------------------------------
    // 1. VALIDAR COMPONENTE
    // --------------------------------------------------------

    if (!IconPlaneComponent)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[Vantage][Slot] IconPlaneComponent no existe.")
        );

        return;
    }

    // --------------------------------------------------------
    // 2. EVITAR INSTANCIAS DUPLICADAS
    // --------------------------------------------------------

    if (DynamicIconMaterial)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[Vantage][Slot][TRACE] MID ya existía; no se crea otro. Actor: %s | MID: %s"),
            *GetName(),
            *GetNameSafe(DynamicIconMaterial)
        );
        return;
    }

    // --------------------------------------------------------
    // 3. VALIDAR ÍNDICE
    // --------------------------------------------------------

    const int32 MaterialCount =
        IconPlaneComponent->GetNumMaterials();

    if (IconMaterialIndex < 0 ||
        IconMaterialIndex >= MaterialCount)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT(
                "[Vantage][Slot] Índice de material inválido. "
                "Índice: %d | Materiales disponibles: %d"
            ),
            IconMaterialIndex,
            MaterialCount
        );

        return;
    }

    // --------------------------------------------------------
    // 4. VALIDAR MATERIAL ORIGINAL
    // --------------------------------------------------------

    UMaterialInterface* OriginalMaterial =
        IconPlaneComponent->GetMaterial(IconMaterialIndex);

    if (!OriginalMaterial)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT(
                "[Vantage][Slot] No existe material asignado "
                "al índice %d de IconPlaneComponent."
            ),
            IconMaterialIndex
        );

        return;
    }

    // --------------------------------------------------------
    // 5. CREAR MATERIAL DINÁMICO
    // --------------------------------------------------------

    DynamicIconMaterial =
        IconPlaneComponent->CreateAndSetMaterialInstanceDynamic(
            IconMaterialIndex
        );

    if (!DynamicIconMaterial)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[Vantage][Slot] No se pudo crear el material dinámico.")
        );

        return;
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[Vantage][Slot][TRACE] MID creado | Actor: %s | MID: %s | Material original: %s | Parámetro esperado: %s"),
        *GetName(),
        *GetNameSafe(DynamicIconMaterial),
        *GetNameSafe(OriginalMaterial),
        *TextureParameterName.ToString()
    );

    /*
     * Una nueva instancia dinámica debe recibir la textura actual,
     * aunque sea la misma textura que se había aplicado anteriormente.
     */
    LastAppliedIconTexture = nullptr;

    UE_LOG(
        LogTemp,
        Log,
        TEXT(
            "[Vantage][Slot] Material dinámico creado. "
            "Material original: %s | Parámetro: %s"
        ),
        *OriginalMaterial->GetName(),
        *TextureParameterName.ToString()
    );
}


// ============================================================
// APLICACIÓN DE TEXTURAS
// ============================================================

/**
 * ApplyIconTexture
 *
 * Aplica la textura correspondiente al activo actual.
 *
 * DIAGNÓSTICO:
 * - Si no existe textura, informa qué símbolo no tiene imagen.
 * - Si no existe material dinámico, informa el problema.
 * - Si la textura cambia, registra cuál se aplicó.
 *
 * Los mensajes permiten distinguir un problema de búsqueda
 * de un problema de inicialización del material.
 */
void ACryptoSlotActor::ApplyIconTexture()
{
    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[Vantage][Slot][TRACE] ApplyIconTexture ENTER | Actor: %s | Símbolo: '%s' | Textura: %s | MID: %s | Parámetro: %s"),
        *GetName(),
        *CurrentAssetData.Symbol,
        *GetNameSafe(CurrentIconTexture),
        *GetNameSafe(DynamicIconMaterial),
        *TextureParameterName.ToString()
    );

    // --------------------------------------------------------
    // 1. VALIDAR TEXTURA
    // --------------------------------------------------------

    if (!CurrentIconTexture)
    {
        /*
         * No reemplazamos el material ni su textura predeterminada.
         *
         * Evitamos repetir el mismo mensaje en cada actualización
         * financiera para no saturar el Output Log.
         */
        if (LastMissingTextureSymbol != CurrentAssetData.Symbol)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT(
                    "[Vantage][Slot] No se recibió textura para "
                    "el símbolo: %s"
                ),
                *CurrentAssetData.Symbol
            );

            LastMissingTextureSymbol = CurrentAssetData.Symbol;
        }

        return;
    }

    // --------------------------------------------------------
    // 2. VALIDAR MATERIAL DINÁMICO
    // --------------------------------------------------------

    if (!DynamicIconMaterial)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT(
                "[Vantage][Slot] No se puede aplicar %s a %s: "
                "el material dinámico no existe."
            ),
            *CurrentIconTexture->GetName(),
            *CurrentAssetData.Symbol
        );

        return;
    }

    // --------------------------------------------------------
    // 3. VALIDAR NOMBRE DEL PARÁMETRO
    // --------------------------------------------------------

    if (TextureParameterName.IsNone())
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT(
                "[Vantage][Slot] TextureParameterName está vacío."
            )
        );

        return;
    }

    // --------------------------------------------------------
    // 4. EVITAR APLICACIONES REDUNDANTES
    // --------------------------------------------------------

    if (LastAppliedIconTexture == CurrentIconTexture)
    {
        return;
    }

    // --------------------------------------------------------
    // 5. APLICAR TEXTURA AL MATERIAL DINÁMICO
    // --------------------------------------------------------

    DynamicIconMaterial->SetTextureParameterValue(
        TextureParameterName,
        CurrentIconTexture
    );

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[Vantage][Slot][TRACE] SetTextureParameterValue ejecutado | Actor: %s | MID: %s | Textura: %s | Parámetro: %s"),
        *GetName(),
        *GetNameSafe(DynamicIconMaterial),
        *GetNameSafe(CurrentIconTexture),
        *TextureParameterName.ToString()
    );

    // Guardar la última textura aplicada.
    LastAppliedIconTexture = CurrentIconTexture;

    // Limpiar el registro de textura faltante.
    LastMissingTextureSymbol.Empty();

    // --------------------------------------------------------
    // 6. REGISTRAR APLICACIÓN CORRECTA
    // --------------------------------------------------------

    UE_LOG(
        LogTemp,
        Log,
        TEXT(
            "[Vantage][Slot] Textura aplicada correctamente. "
            "Símbolo: %s | Textura: %s | Parámetro: %s"
        ),
        *CurrentAssetData.Symbol,
        *CurrentIconTexture->GetName(),
        *TextureParameterName.ToString()
    );
}