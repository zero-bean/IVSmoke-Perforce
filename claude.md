# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**IVSmokeProject**는 **Team SDB (솔ㅊ다빌)**가 Krafton Jungle TechLab 2기 과정에서 개발하는 Unreal Engine 5.7 프로젝트입니다. **IVSmoke 플러그인** - Voxel 기반 Volumetric 연막탄 시뮬레이션 시스템이 핵심입니다.

**Key Characteristics:**
- Engine: Unreal Engine 5.7
- Development Languages: C++ (plugin core) + Blueprints (game logic)
- Version Control: Perforce
- Target Platform: High-end PC (DirectX 12, Shader Model 6)

## What is IVSmoke Plugin?

**IVSmoke** = **I**nteractive **V**olumetric **Smoke**

Voxel 기반의 실시간 Volumetric 연막탄 렌더링 플러그인입니다. Post-Process 렌더링 파이프라인을 활용하여 고품질 연막 효과를 구현합니다.

### Core Technology Stack

1. **Voxel Volume System** - 3D 공간을 Voxel로 분할하여 연막 밀도 표현
2. **Post-Process Rendering** - Scene View Extension을 통한 커스텀 렌더링 패스
3. **Compute Shader** - GPU 기반 Volumetric 연산
4. **Ray Marching** - Voxel 데이터를 활용한 광선 추적 렌더링

### Architecture Principles

- **Public API Only** - FAB 마켓플레이스 출시를 위해 UE5 Public API만 사용
- **No Blueprint Dependencies** - Perforce 병합 충돌 방지를 위해 C++ 전용
- **Modular Design** - Core API 분리로 확장성 확보

## Project Architecture

### Module Structure

#### 1. IVSmokeProject (Main Game Module)
**Location:** `IVSmokeProject/Source/IVSmokeProject/`
- Minimal C++ framework providing base game structure

#### 2. IVSmoke Plugin (Core Volumetric System)
**Location:** `IVSmokeProject/Plugins/IVSmoke/`

**Key Systems:**
1. **Post-Process Pass** - PS/CS dispatch를 위한 재사용 가능한 Core API
2. **Scene View Extension** - 렌더링 파이프라인 훅
3. **Voxel Volume** - Volumetric 데이터 관리
4. **Smoke Renderer** - Volume 기반 연막 렌더링

### File Structure

```
IVSmokeProject/Plugins/IVSmoke/
├── Source/IVSmoke/
│   ├── Public/
│   │   ├── IVSmoke.h                      # 모듈 인터페이스
│   │   ├── IVSmokePostProcessPass.h       # Core API (PS/CS dispatch)
│   │   ├── IVSmokeShaders.h               # Shader 클래스
│   │   ├── IVSmokeSceneViewExtension.h    # Scene hook
│   │   ├── IVSmokeRenderer.h              # Volume 관리
│   │   ├── IVSmokeVolumeComponent.h       # Volume 컴포넌트
│   │   ├── IVSmokeVoxelVolume.h           # Voxel 데이터
│   │   └── IVSmokeHoleData.h              # Hole 데이터
│   ├── Private/
│   │   └── *.cpp
│   └── IVSmoke.Build.cs
└── Shaders/
    └── *.usf                              # HLSL shaders
```

## ⚠️ CRITICAL: Perforce Workflow Protection

**This is a Perforce-managed team project. Follow these rules strictly:**

### File Modification Rules

**NEVER automatically modify, create, or delete files unless:**
1. User explicitly requests it with clear intent ("add", "create", "modify", "delete")
2. You have asked for confirmation if the change impacts multiple files
3. The user is aware this will require a Perforce checkout/submit

**Before any file operation, ask yourself:**
- Did the user explicitly request this file change?
- Will this require checking out files from Perforce?
- Could this conflict with teammates' work?

**If uncertain, ASK the user first. Do NOT assume permission to modify files.**

### Build and Compilation Rules

**NEVER trigger builds or compilation unless:**
1. User explicitly requests: "build", "compile", "regenerate project files"
2. User asks you to verify if code compiles
3. User is debugging a build error and needs to test a fix

**DO NOT:**
- Run build commands "just to check" if code is correct
- Automatically regenerate project files after suggesting code changes
- Compile to verify syntax (use static analysis in your mind instead)
- Build as part of "completing" a task unless explicitly requested

### Read-Only by Default Philosophy

Treat this codebase as **read-only by default**:
- ✅ Reading files to understand code
- ✅ Analyzing architecture
- ✅ Suggesting changes in conversation
- ✅ Providing code snippets for user to apply
- ❌ Writing/editing files without explicit request
- ❌ Running build commands without explicit request
- ❌ Modifying project configuration without explicit request

### Perforce-Specific Cautions

**Files that should NEVER be auto-modified:**
- `.uproject`, `.uplugin` - Project/plugin configuration
- `*.Target.cs`, `*.Build.cs` - Build configuration
- `.p4ignore` - Perforce ignore rules

**Files requiring extra caution:**
- Blueprint files (`.uasset`) - Binary files, merge conflicts are painful
- Data Assets - Often shared across team
- Config files (`Config/*.ini`) - Team-wide settings

### Daily Workflow

```bash
# 1. Start of day - Sync latest
p4 sync //depot/nayechan_dev/IVSmokeProject/...

# 2. Before modifying files - Check out
p4 edit IVSmokeProject/Plugins/IVSmoke/Source/IVSmoke/Private/YourFile.cpp

# 3. End of day - Submit changes
p4 status
p4 submit -d "IVSmoke: Implemented volumetric ray marching"
```

**Best Practices:**
- Sync before starting work each day
- Check out files before editing
- Write clear changelist descriptions mentioning "IVSmoke"
- Submit working code frequently
- Resolve conflicts immediately by coordinating with team

## Build Commands

```bash
# Manual rebuild C++ modules
"C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" IVSmokeProjectEditor Win64 Development -Project="C:\Users\User\Perforce\nayechan_dev\IVSmokeProject\IVSmokeProject.uproject"

# Clean build
"C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Clean.bat" IVSmokeProjectEditor Win64 Development -Project="C:\Users\User\Perforce\nayechan_dev\IVSmokeProject\IVSmokeProject.uproject"

# Regenerate Visual Studio project files (after adding new C++ classes)
"C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\GenerateProjectFiles.bat" "C:\Users\User\Perforce\nayechan_dev\IVSmokeProject\IVSmokeProject.uproject"
```

**Manual Testing:**
- PIE (Play In Editor) - Press `Alt+P`
- Standalone Game - Press `Alt+S`
- Shader Recompile - Press `Ctrl+Shift+.` or `recompileshaders all`

## C++ Coding Conventions

본 프로젝트는 **Epic Games C++ Coding Standard**를 철저히 준수한다.

### 제어문 (Control Structures)

- **중괄호 강제**: 조건문이나 루프의 내용이 한 줄이라도 **반드시 중괄호 `{ }`를 사용**한다.

```cpp
// [Bad]
if (bIsDead) return;

// [Good]
if (bIsDead)
{
    return;
}
```

### 여러 줄 괄호 (Multi-line Parentheses)

- **닫는 괄호 분리**: 함수 호출이나 선언이 여러 줄인 경우, 마지막 닫는 괄호 `)`를 다음 줄로 넘긴다.

```cpp
// [Good]
FPixelShaderUtils::AddFullscreenPass(
    GraphBuilder,
    ShaderMap,
    RDG_EVENT_NAME("%s PS", Config.EventName),
    PixelShader,
    Parameters,
    Output.ViewRect,
    Config.BlendState
);

// [Bad]
FPixelShaderUtils::AddFullscreenPass(
    GraphBuilder,
    ShaderMap,
    RDG_EVENT_NAME("%s PS", Config.EventName),
    PixelShader,
    Parameters,
    Output.ViewRect,
    Config.BlendState);
```

### auto 키워드 사용 (Auto Keyword)

제한적으로 허용:
1. **타입이 명확할 때**: 캐스팅이나 생성자 호출로 우변에서 타입을 바로 알 수 있는 경우
2. **반복자(Iterator)**: 템플릿 타입 이름이 지나치게 길 때

```cpp
// [Good]
auto* Character = Cast<AMyCharacter>(OtherActor);
for (auto It = Map.CreateIterator(); It; ++It) { ... }

// [Bad] - 명시적 타입 사용 필요
auto MyWeapon = GetWeapon();
```

### 헤더 포함 (Includes & Forward Declaration)

- **전방 선언 (Forward Declaration)**: 헤더에서는 `class AMyActor;` 형태로, `#include`는 소스 파일에서
- **Engine Minimal**: 불필요한 엔진 헤더를 포함하지 않음

### 포인터와 Null (Pointers)

- **nullptr 사용**: C 스타일의 `NULL` 대신 `nullptr`
- **유효성 검사**: 역참조 전 `IsValid()`, `ensure()`, 또는 `if (Ptr)`로 검증

### const 정확성 (Const Correctness)

- **Rule**: 멤버 변수를 수정하지 않는 함수는 `const`로 선언
- **Parameter Passing**:
  - 기본 타입 (int, float, bool): Call by Value
  - 복합 타입 (FString, FVector, TArray): `const Type&`
  - 포인터: 대상 수정 안 하면 `const Type*`

```cpp
// [Good]
void SetName(const FString& NewName, const FMyStruct& Data);
void SetHealth(float NewHealth);
```

### 접두사 (Naming Prefixes)

| **Prefix** | **Type** | **Example** |
| --- | --- | --- |
| **A** | `AActor` 상속 클래스 | `ASmokeGrenade`, `AMyCharacter` |
| **U** | `UObject` 상속 클래스 (Actor 제외) | `UIVSmokeVolumeComponent` |
| **F** | 구조체 및 일반 C++ 클래스 | `FIVSmokePassConfig` |
| **E** | 열거형 | `EIVSmokeRenderMode` |
| **I** | 인터페이스 | `IInteractable` |
| **b** | 불리언 | `bIsActive` |
| **S** | 슬레이트 위젯 | `SMyButton` |

### 클래스 구조

**전략 A: 일반 클래스 (Standard)** - 300~500줄 이하, 단일 책임
- 구조: `GENERATED_BODY()` → `public` (생성자, API) → `protected` (오버라이드) → `private` (구현, 변수)

**전략 B: 복합 클래스 (Monolithic)** - 500줄+, 다수 하위 시스템
- `#pragma region` 사용, 기능별 구역 분리, 관련 변수/함수 물리적 근접 배치
- 섹션 디바이더 및 Region 사용법은 "주석" 섹션 참조

### 오류 처리 & 로깅

**Log Category:**
```cpp
DECLARE_LOG_CATEGORY_EXTERN(LogIVSmoke, Log, All);  // 헤더
DEFINE_LOG_CATEGORY(LogIVSmoke);                     // 소스
```

**처리 기준:**

| **매크로** | **Editor** | **Shipping** | **사용 시점** |
| --- | --- | --- | --- |
| `check(Expr)` | 크래시 | 코드 제거 | 메모리 오염, 치명적 로직 오류 |
| `ensure(Expr)` | Callstack 출력 후 계속 | 코드 제거 | 예상치 못한 상태지만 복구 가능 |
| `UE_LOG(LogIVSmoke, Error, ...)` | 빨간 로그 | 로그 출력 | 데이터 설정 오류 |
| `UE_LOG(LogIVSmoke, Warning, ...)` | 노란 로그 | 로그 출력 | 단순 경고 |
| `UE_LOG(LogIVSmoke, Log, ...)` | 회색 로그 | 출력 안 함 | 디버그 정보 |

**로깅 포맷:** `"[ClassName::FunctionName] Message : DetailInfo"`

```cpp
UE_LOG(LogIVSmoke, Error, TEXT("[FIVSmokeRenderer::Render] Volume is invalid : VolumePtr = %p"), VolumePtr);
```

### 프로퍼티 & 함수 카테고리

- **루트:** `IVSmoke`로 시작
- **계층:** 파이프라인 `|`로 구분, 최대 3단계
- **형식:** `Category = "IVSmoke|MainCategory|SubCategory"`

```cpp
UPROPERTY(EditAnywhere, Category = "IVSmoke|Volume")
float VoxelSize = 10.0f;

UPROPERTY(EditAnywhere, Category = "IVSmoke|Rendering")
float SmokeDensity = 0.5f;
```

### 주석 (Comments)

기본적으로 **최신 Unreal Engine 코딩 스타일**을 따른다.

**Doxygen 형식** - 모든 public API에 적용:

```cpp
/**
 * Brief description (한 줄 요약)
 *
 * @param ParamName Description of parameter
 * @return Description of return value
 */
```

**섹션 디바이더** - 코드 내 논리적 구역 구분:

```cpp
//~==============================================================================
// Section Name (예: Flood Fill Simulation)
```

**Region 사용** - 소스코드가 길어질 경우 (500줄+):

```cpp
#pragma region Section Name

// ... code ...

#pragma endregion
```

### 콘솔 커맨드 (Console Commands)

**네이밍:** `IVSmoke.CommandName` (PascalCase)

| Category | Prefix | Example |
|----------|--------|---------|
| Render Mode | IVSmoke.* | `IVSmoke.RenderMode` |
| Debug | IVSmoke.Debug* | `IVSmoke.DebugVolume` |
| Quality | IVSmoke.Quality* | `IVSmoke.QualityLevel` |

**구현 예시:**

```cpp
static TAutoConsoleVariable<int32> CVarIVSmokeRenderMode(
    TEXT("IVSmoke.RenderMode"),
    0,
    TEXT("0: Pixel Shader, 1: Compute Shader"),
    ECVF_RenderThreadSafe
);
```

**디버그 전용:**
```cpp
#if !UE_BUILD_SHIPPING
static FAutoConsoleCommand DebugCommand(...);
#endif
```

## File Paths Reference

```
C:\Users\User\Perforce\nayechan_dev\
├── .p4ignore                      # Perforce ignore rules
├── claude.md                      # Claude Code guidance
└── IVSmokeProject\
    ├── IVSmokeProject.uproject
    ├── Source\IVSmokeProject\     # Main game module
    ├── Plugins\IVSmoke\           # Volumetric smoke plugin
    │   ├── IVSmoke.uplugin
    │   ├── Source\IVSmoke\
    │   │   ├── Public\
    │   │   ├── Private\
    │   │   └── IVSmoke.Build.cs
    │   ├── Shaders\               # HLSL shader files
    │   └── Docs\                  # Plugin documentation
    ├── Content\                   # Main content folder
    └── Config\                    # Project configuration
```

## Documentation Organization

- **`claude.md`** (root) - Project-wide guidance
- **`Plugins/IVSmoke/Docs/`** - Plugin-specific documentation

**Naming:** `IVSmoke_<TYPE>.md` (PascalCase)
- Examples: `IVSmoke_DeveloperGuide.md`, `IVSmoke_ShaderReference.md`

**Do NOT create documentation files without explicit user request.**

## Resources & References

**Volumetric Rendering:**
- "Real-Time Volumetric Cloudscapes" - Andrew Schneider (SIGGRAPH 2015)
- "Physically Based Sky, Atmosphere and Cloud Rendering" - Sébastien Hillaire

**UE5 Documentation:**
- Post Process Materials: https://docs.unrealengine.com/5.7/post-process-materials
- Scene View Extensions: https://docs.unrealengine.com/5.7/graphics-programming
- Global Shaders: https://docs.unrealengine.com/5.7/global-shaders

## Quick Reference

### Before You Start Coding
1. ✅ Sync latest from Perforce
2. ✅ Understand which system you're modifying
3. ✅ Read related code first

### Before You Submit
1. ✅ Code compiles without warnings
2. ✅ Tested in PIE
3. ✅ Shader compiles correctly (`Ctrl+Shift+.`)
4. ✅ No debug logs in shipping code
5. ✅ Clear changelist description

### When You're Stuck
1. Check this claude.md
2. Check `Plugins/IVSmoke/Docs/IVSmoke_DeveloperGuide.md`
3. Use debug visualization (`IVSmoke.RenderMode`)
4. Profile before optimizing
