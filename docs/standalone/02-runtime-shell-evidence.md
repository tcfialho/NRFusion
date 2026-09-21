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

## Revisão independente — 2026-09-21

A segunda passada encontrou e corrigiu dois casos fail-closed que os testes originais não cobriam:

- um plano ativo com Provider+Executor aceitava retry contendo só Provider como se fosse o mesmo plano;
- `RuntimeComponentRegistry::Supports()` aceitava máscara de capability zero quando a chave existia.

Correções:

- `MatchesPlan()` agora canonicaliza o plano em registry temporário fixo e exige mesmo conjunto/máscaras;
- `Supports()` rejeita capability mask zero;
- regressões adicionadas a `runtime_shell_tests`;
- compile/test independente C++20 com `-Wall -Wextra -Wpedantic -Werror`: **PASS**.

Commits da revisão: `33ca0214a2636a7db7f5279eb73df23a0b6a9fb0`,
`0adfacbdb13642a616ae0722bb378d758ec6ff42`.

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
