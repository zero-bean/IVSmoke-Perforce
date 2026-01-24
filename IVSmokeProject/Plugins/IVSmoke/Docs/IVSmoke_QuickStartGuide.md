# IVSmoke Quick Start Guide {#mainpage}

**IVSmoke** (Interactive Volumetric Smoke) is a real-time voxel-based volumetric smoke plugin for Unreal Engine 5.7. This guide covers the essential steps to get started.

---

## Table of Contents

1. [Installation](#installation)
2. [Basic Usage](#basic-usage)
3. [Project Settings](#project-settings)
4. [Volume Configuration](#volume-configuration)
5. [Smoke Preset](#smoke-preset)
6. [Hole Preset](#hole-preset)
7. [Interactive Features](#interactive-features)
8. [Network Replication](#network-replication)

---

## Installation

1. Copy the `IVSmoke` folder to your project's `Plugins` directory
2. Regenerate project files
3. Open your project in Unreal Editor
4. Enable the plugin in **Edit > Plugins > IVSmoke**

**Requirements:**

- Unreal Engine 5.7
- Windows 10+ (DirectX 11.3+ / Shader Model 5)
- Win64 platform

---

## Basic Usage

### Adding Smoke to Your Level

1. In the **Place Actors** panel, search for `IVSmokeVoxelVolume`
2. Drag the actor into your level
3. Position and scale the volume as needed
4. Play the level to see the smoke simulation

The smoke volume automatically progresses through lifecycle phases:

- **Idle** → **Expansion** → **Sustain** → **Dissipation** → **Finished**

### Triggering Smoke

By default, `bAutoActivate` is enabled. To manually control activation:

```cpp
// C++
AIVSmokeVoxelVolume* SmokeVolume = ...;
SmokeVolume->ActivateSmoke();
```

```
// Blueprint
Get reference to IVSmokeVoxelVolume → Call "Activate Smoke"
```

---

## Project Settings

Access via **Edit > Project Settings > Plugins > IVSmoke**

### Quality Levels

| Setting           | Description                                     |
| ----------------- | ----------------------------------------------- |
| **Quality Level** | Overall quality preset (Low/Medium/High/Custom) |

| Level      | MaxSteps     | MinStepSize  | Description                      |
| ---------- | ------------ | ------------ | -------------------------------- |
| **Low**    | 128          | 50.0         | Fast performance, lower quality  |
| **Medium** | 256          | 25.0         | Balanced quality and performance |
| **High**   | 512          | 16.0         | Best quality, higher GPU cost    |
| **Custom** | User-defined | User-defined | Manual control over parameters   |

### Noise Settings

| Setting              | Description                 | Default |
| -------------------- | --------------------------- | ------- |
| **Noise Resolution** | 3D noise texture resolution | 64      |
| **Noise Scale**      | Noise pattern scale         | 1.0     |
| **Noise Speed**      | Animation speed             | 0.5     |
| **Noise Strength**   | Effect intensity            | 0.3     |

### Appearance

| Setting              | Description                  | Default |
| -------------------- | ---------------------------- | ------- |
| **Smoke Color**      | Base smoke color             | Gray    |
| **Smoke Absorption** | Light absorption coefficient | 1.0     |
| **Volume Density**   | Overall density multiplier   | 1.0     |
| **Density Scale**    | Additional density scaling   | 1.0     |

### Lighting

| Setting                     | Description                   | Default |
| --------------------------- | ----------------------------- | ------- |
| **Ambient Light Color**     | Ambient lighting tint         | White   |
| **Ambient Light Intensity** | Ambient brightness            | 0.3     |
| **Enable Scattering**       | Light scattering effect       | true    |
| **Scattering Scale**        | Scattering intensity          | 1.0     |
| **Scattering Anisotropy**   | Henyey-Greenstein G parameter | 0.3     |

### Shadows

| Setting                          | Description                | Default |
| -------------------------------- | -------------------------- | ------- |
| **Enable External Shadows**      | Receive shadows from scene | false   |
| **Shadow Ambient**               | Minimum shadow brightness  | 0.1     |
| **Enable VSM**                   | Variance Shadow Maps       | true    |
| **VSM Min Variance**             | VSM minimum variance       | 0.0001  |
| **VSM Light Bleeding Reduction** | Light bleeding fix         | 0.2     |

### Rendering

| Setting              | Description             | Default |
| -------------------- | ----------------------- | ------- |
| **Max Step Count**   | Ray marching iterations | 64      |
| **Step Size**        | Ray march step distance | 10.0    |
| **Enable FXAA**      | Anti-aliasing           | false   |
| **Enable Sharpen**   | Sharpening filter       | false   |
| **Sharpen Strength** | Sharpening intensity    | 0.5     |

---

## Volume Configuration

Select an `AIVSmokeVoxelVolume` actor to configure these properties:

### Grid Settings (Category: IVSmoke | Grid)

| Property            | Description                     | Default      |
| ------------------- | ------------------------------- | ------------ |
| **Voxel Size**      | Size of each voxel in cm        | 25.0         |
| **Grid Resolution** | Voxel grid dimensions (X, Y, Z) | (16, 16, 16) |

### Simulation Timing (Category: IVSmoke | Simulation)

| Property                 | Description                    | Default |
| ------------------------ | ------------------------------ | ------- |
| **Expansion Duration**   | Time to fully expand (seconds) | 2.0     |
| **Sustain Duration**     | Time at full density (seconds) | 5.0     |
| **Dissipation Duration** | Time to fade out (seconds)     | 3.0     |
| **Auto Activate**        | Start simulation automatically | true    |

### Appearance (Category: IVSmoke | Appearance)

| Property         | Description                                 |
| ---------------- | ------------------------------------------- |
| **Smoke Preset** | Reference to UIVSmokeSmokePreset data asset |

---

## Smoke Preset

Create a **Smoke Preset** data asset for reusable appearance settings:

1. **Content Browser** → Right-click → **Miscellaneous > Data Asset**
2. Select `IVSmokeSmokePreset` as the class
3. Configure the preset properties:

| Property             | Description            | Default         |
| -------------------- | ---------------------- | --------------- |
| **Smoke Color**      | RGB color of the smoke | (0.5, 0.5, 0.5) |
| **Smoke Absorption** | Light absorption rate  | 1.0             |
| **Volume Density**   | Density multiplier     | 1.0             |

4. Assign the preset to your `AIVSmokeVoxelVolume` actor's **Smoke Preset** property

---

## Hole Preset

Create a **Hole Preset** data asset for reusable hole configuration:

1. **Content Browser** → Right-click → **Miscellaneous > Data Asset**
2. Select `IVSmokeHolePreset` as the class
3. Configure the preset based on hole type

### Hole Types

| Type            | Description                              | Use Case                    |
| --------------- | ---------------------------------------- | --------------------------- |
| **Penetration** | Cylindrical hole along a path            | Bullets, projectiles        |
| **Explosion**   | Spherical expanding hole with distortion | Grenades, explosions        |
| **Dynamic**     | Box-shaped hole that follows an object   | Moving vehicles, characters |

### Common Properties

| Property      | Description                             | Default     |
| ------------- | --------------------------------------- | ----------- |
| **Hole Type** | Penetration / Explosion / Dynamic       | Penetration |
| **Radius**    | Effect radius in cm (not for Dynamic)   | 50.0        |
| **Duration**  | How long the hole lasts (seconds)       | 3.0         |
| **Softness**  | Edge softness (0=hard, 1=soft gradient) | 0.3         |

### Explosion-Specific Properties

| Property                           | Description                          | Default |
| ---------------------------------- | ------------------------------------ | ------- |
| **Expansion Duration**             | Time for hole to reach full size     | 0.15    |
| **Expansion Fade Range Curve**     | Fade range over expansion time (0-1) | -       |
| **Shrink Fade Range Curve**        | Fade range during shrink phase       | -       |
| **Shrink Density Mul Curve**       | Density multiplier during shrink     | -       |
| **Distortion Curve Over Time**     | Distortion intensity over time       | -       |
| **Distortion Distance**            | Maximum distortion distance          | 0.0     |
| **Distortion Curve Over Distance** | Distortion falloff by distance       | -       |

**Explosion Timeline:**

1. **Expansion Phase** (0 → ExpansionDuration): Hole expands to full radius
2. **Shrink Phase** (ExpansionDuration → Duration): Hole gradually closes

### Penetration-Specific Properties

| Property       | Description              | Default |
| -------------- | ------------------------ | ------- |
| **End Radius** | Radius at the exit point | 25.0    |

### Dynamic-Specific Properties

| Property               | Description                        | Default      |
| ---------------------- | ---------------------------------- | ------------ |
| **Extent**             | Box size (X, Y, Z)                 | (50, 50, 50) |
| **Distance Threshold** | Min travel distance to create hole | 50.0         |

---

## Interactive Features

The `UIVSmokeHoleGeneratorComponent` provides APIs for creating holes in the smoke volume.
For detailed hole configuration, create a **Hole Preset** data asset (see [Hole Preset](#hole-preset) section).

### Adding the Component

```cpp
// C++ - Add to your Actor
UIVSmokeHoleGeneratorComponent* HoleGenerator = CreateDefaultSubobject<UIVSmokeHoleGeneratorComponent>(TEXT("HoleGenerator"));
```

### Penetration Holes (Bullets)

Creates a cylindrical hole along a projectile path. Requires a Penetration-type Hole Preset:

```cpp
// C++
UIVSmokeHolePreset* BulletPreset = LoadObject<UIVSmokeHolePreset>(...);
HoleGenerator->RequestPenetrationHole(
    FVector3f(HitLocation),  // Origin: Impact point
    FVector3f(ShotDirection), // Direction: Projectile direction (normalized)
    BulletPreset              // Preset: Penetration-type hole preset
);
```

```
// Blueprint
Request Penetration Hole
- Origin: Hit location (FVector3f)
- Direction: Shot direction (FVector3f)
- Preset: Reference to IVSmokeHolePreset (Penetration type)
```

### Explosion Holes

Creates a spherical expanding hole with optional distortion effects.
Requires an Explosion-type Hole Preset for animated expansion/shrink with curve-based control:

```cpp
// C++
UIVSmokeHolePreset* GrenadePreset = LoadObject<UIVSmokeHolePreset>(...);
HoleGenerator->RequestExplosionHole(
    FVector3f(ExplosionCenter),  // Origin: Center of explosion
    GrenadePreset                 // Preset: Explosion-type hole preset
);
```

```
// Blueprint
Request Explosion Hole
- Origin: Explosion center (FVector3f)
- Preset: Reference to IVSmokeHolePreset (Explosion type)
```

**Tip:** Create an Explosion-type `UIVSmokeHolePreset` to customize:

- Expansion/shrink timing and curves
- Smoke distortion effects (push smoke outward)
- Edge softness and duration

### Dynamic Object Tracking

Tracks a moving actor and creates holes along its path.
Requires a Dynamic-type Hole Preset:

```cpp
// C++
UIVSmokeHolePreset* VehiclePreset = LoadObject<UIVSmokeHolePreset>(...);

// Start tracking
HoleGenerator->RequestTrackDynamicObject(
    TargetActor,    // AActor*: Actor to follow
    VehiclePreset   // Preset: Dynamic-type hole preset
);
```

```
// Blueprint
Request Track Dynamic Object
- Target Actor: Actor reference to track
- Preset: Reference to IVSmokeHolePreset (Dynamic type)
```

---

## Network Replication

IVSmoke supports network replication for multiplayer games.

### Enabling Replication

On your `AIVSmokeVoxelVolume` actor:

| Property                 | Description              | Default |
| ------------------------ | ------------------------ | ------- |
| **Replicate Smoke**      | Enable network sync      | false   |
| **Replication Interval** | Sync frequency (seconds) | 0.1     |

### How It Works

- **Server**: Runs the full simulation and broadcasts state
- **Clients**: Receive replicated voxel data and render locally
- Hole generation is automatically replicated when enabled

### Best Practices

1. Only enable replication when needed (has bandwidth cost)
2. Adjust `ReplicationInterval` based on your network requirements
3. For client-side-only effects, keep `ReplicateSmoke` disabled

---

## Troubleshooting

### Smoke Not Visible

1. Check that the volume is within camera view
2. Verify `bAutoActivate` is enabled or call `ActivateSmoke()`
3. Ensure Project Settings density values are > 0

### Performance Issues

1. Reduce **Grid Resolution** (fewer voxels)
2. Increase **Voxel Size** (larger voxels, less detail)
3. Lower **Max Step Count** in Project Settings
4. Disable **External Shadows** if not needed

### Network Sync Issues

1. Verify `bReplicateSmoke` is enabled on server
2. Check that the actor is set to replicate (`bReplicates = true`)
3. Adjust `ReplicationInterval` if updates are too slow

---

## API Reference

For detailed API documentation, see Doxygen documentation or refer to the header files:

- `IVSmokeVoxelVolume.h` - Main smoke volume actor
- `IVSmokeSettings.h` - Project settings
- `IVSmokeSmokePreset.h` - Appearance presets
- `IVSmokeHolePreset.h` - Hole configuration presets (Penetration/Explosion/Dynamic)
- `IVSmokeHoleGeneratorComponent.h` - Hole generation API

---

*Copyright (c) 2026, Team SDB. All rights reserved.*
