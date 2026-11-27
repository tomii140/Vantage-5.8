
#include "ACryptoNavigationActor.h"

// Terminal principal.
#include "ACryptoTerminalActor.h"

// Clase base nativa del widget de navegación.
#include "CryptoNavigationWidget.h"

// Componentes.
#include "Components/SceneComponent.h"
#include "Components/WidgetComponent.h"


// ============================================================
// CONSTRUCTOR
// ============================================================

/**
 * Construye un Actor independiente para la interfaz de navegación.
 *
 * La jerarquía es deliberadamente pequeña:
 *
 * RootComponent
 * └── NavigationWidgetComponent
 *
 * Este Actor no se adjunta al terminal. Por lo tanto, su ubicación,
 * rotación y escala en el mundo son independientes de los slots.
 */
ACryptoNavigationActor::ACryptoNavigationActor()
{
    PrimaryActorTick.bCanEverTick = false;

    // --------------------------------------------------------
    // 1. ROOT DEL ACTOR DE NAVEGACIÓN
    // --------------------------------------------------------

    USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(
        TEXT("SceneRoot")
    );

    RootComponent = SceneRoot;

    // --------------------------------------------------------
    // 2. COMPONENTE DEL WIDGET
    // --------------------------------------------------------

    NavigationWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(
        TEXT("NavigationWidgetComponent")
    );

    NavigationWidgetComponent->SetupAttachment(RootComponent);

    NavigationWidgetComponent->SetWidgetSpace(EWidgetSpace::World);

    // Aplicar las dimensiones iniciales configuradas en el header.
    NavigationWidgetComponent->SetDrawSize(NavigationDrawSize);

    // Mantener el punto de anclaje en el centro del widget.
    NavigationWidgetComponent->SetPivot(FVector2D(0.5f, 0.5f));
}


// ============================================================
// CONFIGURACIÓN VISUAL Y TRANSFORMACIÓN
// ============================================================

/**
 * Aplica las propiedades configuradas desde el editor.
 *
 * Se ejecuta durante la construcción del Actor y permite modificar
 * dimensiones y transformaciones sin iniciar el juego.
 */
void ACryptoNavigationActor::OnConstruction(
    const FTransform& Transform
)
{
    Super::OnConstruction(Transform);

    ApplyNavigationWidgetSettings();
}


/**
 * Aplica dimensiones y transformación al WidgetComponent.
 *
 * Las dimensiones se limitan a valores positivos para evitar
 * configuraciones inválidas del espacio de dibujo.
 */
void ACryptoNavigationActor::ApplyNavigationWidgetSettings()
{
    if (!IsValid(NavigationWidgetComponent))
    {
        return;
    }

    // --------------------------------------------------------
    // 1. DIMENSIONES DEL WIDGET
    // --------------------------------------------------------

    const FVector2D SafeDrawSize(
        FMath::Max(1.0f, NavigationDrawSize.X),
        FMath::Max(1.0f, NavigationDrawSize.Y)
    );

    NavigationWidgetComponent->SetDrawSize(SafeDrawSize);

    // --------------------------------------------------------
    // 2. POSICIÓN LOCAL
    // --------------------------------------------------------

    NavigationWidgetComponent->SetRelativeLocation(
        NavigationRelativeLocation
    );

    // --------------------------------------------------------
    // 3. ROTACIÓN LOCAL
    // --------------------------------------------------------

    NavigationWidgetComponent->SetRelativeRotation(
        NavigationRelativeRotation
    );

    // --------------------------------------------------------
    // 4. ESCALA LOCAL
    // --------------------------------------------------------

    NavigationWidgetComponent->SetRelativeScale3D(
        NavigationRelativeScale
    );
}


// ============================================================
// CICLO DE VIDA
// ============================================================

/**
 * Vincula el Actor de navegación con el terminal y crea el UserWidget.
 *
 * La referencia TerminalReference se asigna en el editor.
 * No se utiliza GetAllActorsOfClass ni ninguna búsqueda global.
 */
void ACryptoNavigationActor::BeginPlay()
{
    Super::BeginPlay();

    ApplyNavigationWidgetSettings();

    if (!IsValid(TerminalReference))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[Vantage][NavigationActor] TerminalReference no está asignada. Asignar ACryptoTerminalActor en Details.")
        );

        return;
    }

    // Escuchar los cambios de paginación del terminal.
    TerminalReference->OnTerminalNavigationUpdated.AddUniqueDynamic(
        this,
        &ACryptoNavigationActor::HandleTerminalNavigationUpdated
    );

    InitializeNavigationWidget();
}


/**
 * Desconecta el evento y limpia la referencia del widget.
 *
 * La limpieza evita que el UserWidget conserve una referencia al
 * terminal cuando este Actor de navegación termina su ciclo de vida.
 */
void ACryptoNavigationActor::EndPlay(
    const EEndPlayReason::Type EndPlayReason
)
{
    if (IsValid(TerminalReference))
    {
        TerminalReference->OnTerminalNavigationUpdated.RemoveDynamic(
            this,
            &ACryptoNavigationActor::HandleTerminalNavigationUpdated
        );
    }

    if (IsValid(NavigationWidgetComponent))
    {
        if (UCryptoNavigationWidget* NavigationWidget =
            Cast<UCryptoNavigationWidget>(
                NavigationWidgetComponent->GetUserWidgetObject()
            ))
        {
            NavigationWidget->SetTerminalReference(nullptr);
        }
    }

    TerminalReference = nullptr;

    Super::EndPlay(EndPlayReason);
}


// ============================================================
// INICIALIZACIÓN DEL WIDGET
// ============================================================

/**
 * Inicializa el UserWidget asignado en Widget Class.
 *
 * Se conserva la clase base UCryptoNavigationWidget y el método
 * SetTerminalReference existente. No requiere modificar el WBP.
 */
void ACryptoNavigationActor::InitializeNavigationWidget()
{
    if (!IsValid(NavigationWidgetComponent))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[Vantage][NavigationActor] NavigationWidgetComponent inválido.")
        );

        return;
    }

    if (!NavigationWidgetComponent->GetWidgetClass())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[Vantage][NavigationActor] No hay Widget Class asignada. Configurar WBP_CryptoNavigation.")
        );

        return;
    }

    NavigationWidgetComponent->InitWidget();

    UUserWidget* UserWidget =
        NavigationWidgetComponent->GetUserWidgetObject();

    if (!IsValid(UserWidget))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[Vantage][NavigationActor] No se pudo crear el UserWidget.")
        );

        return;
    }

    UCryptoNavigationWidget* NavigationWidget =
        Cast<UCryptoNavigationWidget>(UserWidget);

    if (!IsValid(NavigationWidget))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[Vantage][NavigationActor] WBP_CryptoNavigation no utiliza CryptoNavigationWidget como clase padre.")
        );

        return;
    }

    // Entregar la referencia directa al terminal.
    NavigationWidget->SetTerminalReference(TerminalReference);

    // Sincronizar el contador desde el primer momento.
    NavigationWidget->RefreshNavigation();

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[Vantage][NavigationActor] Widget inicializado y vinculado al terminal: %s"),
        *GetNameSafe(TerminalReference)
    );
}


// ============================================================
// ACTUALIZACIÓN DEL WIDGET
// ============================================================

/**
 * Callback del evento OnTerminalNavigationUpdated.
 *
 * El terminal emite este evento cuando cambia la página o se actualiza
 * el catálogo. El Actor independiente refresca el widget.
 */
void ACryptoNavigationActor::HandleTerminalNavigationUpdated()
{
    RefreshNavigationWidget();
}


/**
 * Actualiza el estado visual del UserWidget.
 */
void ACryptoNavigationActor::RefreshNavigationWidget()
{
    if (!IsValid(NavigationWidgetComponent))
    {
        return;
    }

    UCryptoNavigationWidget* NavigationWidget =
        Cast<UCryptoNavigationWidget>(
            NavigationWidgetComponent->GetUserWidgetObject()
        );

    if (!IsValid(NavigationWidget))
    {
        return;
    }

    NavigationWidget->RefreshNavigation();
}
