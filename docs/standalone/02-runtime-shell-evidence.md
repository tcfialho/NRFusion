# Fase 02 — Evidência do runtime shell

## Runtime shell

Novos componentes:

- `RuntimeConfig`: generation, Enabled, Auto/BestQuality/Performance/Custom e target FPS;
- `RuntimeComponentRegistry`: array fixo de 16 entries, sem heap;
- `RuntimeShell`: lifecycle, status e reconfigure;
- `RuntimeBootstrap`: startup transacional + rollback.

O shell não contém filesystem, COM, hooks, strings de config nem tipos de API específicos.

### Ownership

`RuntimeShell` é dono de:

- snapshot de config;
- status;
- registry.

O registry mutável é privado e só `RuntimeBootstrap` pode preenchê-lo. Leitores recebem visão const.

### Config generation

- geração menor: rejeitada;
- mesma geração + mesmo conteúdo: idempotente;
- mesma geração + conteúdo diferente: rejeitada;
- geração maior válida: aplicada.

O standalone não reutiliza `UserMode::Balanced`; o contrato novo usa somente
Auto / BestQuality / Performance / Custom.

### Bootstrap

`Start()`:

1. é no-op para o mesmo plano já ativo;
2. rejeita plano diferente quando já iniciado;
3. valida config;
4. registra capabilities;
5. faz rollback completo se qualquer componente for inválido.

`Stop()` é idempotente.

## GameProbe

O antigo `GameProbe.cpp` de ~628 linhas foi dividido em:

| Arquivo | Linhas |
|---|---:|
| `GameProbe.cpp` | 136 |
| `GameProbeInspect.cpp` | 157 |
| `GameProbeDetection.cpp` | 203 |
| `GameProbeSupport.cpp` | 138 |
| `GameProbeInternal.hpp` | 48 |

Responsabilidades:

- Inspect: leitura/PE imports;
- Detection: heurísticas API/capabilities/DXVK;
- Probe: orchestration;
- Support: integrated capabilities/install-support.

Comparação automática de corpos mostrou equivalência em todas as funções movidas. `ReadPrefix`
teve apenas a remoção do parâmetro default interno, que nunca possuía caller com segundo argumento.

## CMake

Antes: um `CMakeLists.txt` de 266 linhas.

Depois:

| Arquivo | Linhas |
|---|---:|
| `CMakeLists.txt` | 13 |
| `NRFusionCore.cmake` | 86 |
| `NRFusionTests.cmake` | 18 |
| `NRFusionTools.cmake` | 6 |
| `NRFusionWindows.cmake` | 127 |

Comparação de target/source sets:

- targets antigos removidos: 0;
- tests antigos removidos: 0;
- fontes antigas removidas: 0;
- novo test target: `nrfusion_runtime_shell_tests`;
- novas fontes: split do GameProbe + RuntimeShell/RuntimeBootstrap.

Configure local do CMake modular: **PASS**.

## Performance / validação

Runtime shell local:

```text
C++20 -Wall -Wextra -Wpedantic -Werror
runtime shell compile/test: PASS
bootstrap rollback/idempotence: PASS
invalid config/enum/component: PASS
```

Disabled path, 5.000.000 reconfigures idempotentes, build `-O3`:

```text
~3.18 ns/call
heap allocations: 0
```

Esse número mede somente o shell portátil no ambiente local; não representa frametime de jogo.

## Source-size

Maior arquivo first-party novo/tocado nesta fase: `GameProbeDetection.cpp`, 203 linhas.
Nenhum arquivo tocado >300 e nenhum comentário novo >120 caracteres.

## Limites deliberados

- ainda não existe executor standalone;
- ainda não existe carrier/proxy standalone;
- o shell não é consumido pelo frame path;
- concorrência render/UI fica para o snapshot operacional do `NrSession` na Fase 06;
- build_dist/installer continuam legacy e só mudam nas fases próprias;
- nenhum full CI foi disparado nesta fase.

## Gate Fase 02

Fechado. Próxima fase: Development Harness.
