# Fase 02 — Standalone runtime shell

## Status

**Concluída após terceira revisão adversarial.** Evidência: [02-runtime-shell-evidence.md](02-runtime-shell-evidence.md).

## Objetivo

Criar lifecycle/bootstrap/configuração sem concentrar tudo em runtime, probe ou build script gigante.

## Implementação

- [x] Separar bootstrap, runtime lifecycle, config snapshot e registry.
- [x] Manter o bootstrap independente de proxy/API gráfica.
- [x] Separar GameProbe em PE inspection, API/capability detection, orchestration e install support.
- [x] Dividir CMake em Core, Tests, Tools e Windows sem remover targets existentes.
- [x] Não tocar em `build_dist.ps1`.
- [x] Definir init/shutdown idempotentes.
- [x] Criar `RuntimeConfig` compacto com generation e modos standalone.
- [x] Fazer a configuração default fail-closed: disabled até enable explícito.
- [x] Registrar Provider/Executor por API + capability mask em storage fixo.
- [x] Expor status/failure reason sem strings no hot path.
- [x] Manter o novo shell sem dependência de OptiScaler Config/State/patcher.
- [x] Disabled path sem heap allocation.
- [x] Cada módulo novo/tocado <=300 linhas.

## Revisão obrigatória

- [x] Shell possui apenas config/status/registry; não possui COM/module/hook ainda.
- [x] Bootstrap faz rollback completo em config/componente inválido.
- [x] Bootstrap não pressupõe D3D12 nem outra API.
- [x] Filesystem/probing permanece fora do runtime shell.
- [x] Build split preserva targets/testes antigos e não cria build adicional.
- [x] Headers standalone não dependem de `<cstdint>` transitivo para seus próprios tipos.
- [x] Comentários movidos para arquivos novos foram reduzidos a justificativas curtas.

## Validação rápida

- [x] Runtime shell compilado com warnings-as-errors.
- [x] Init/reconfigure/shutdown e bootstrap idempotente validados.
- [x] Config inválida via `RuntimeBootstrap` faz rollback e retorna `InvalidConfig`.
- [x] Falha após registro parcial faz rollback.
- [x] Invalid mode/kind/API/capability mask passam fail-closed.
- [x] Disabled path medido em 5.000.000 chamadas, 0 allocations.
- [x] CMake modular configurado em projeto espelho sem compilar o projeto pesado.
- [x] GameProbe dividido comparado função-a-função com o original.
- [x] Line count auditado para todo arquivo novo/tocado.

## Gate

- [x] Shutdown sem estado residual controlado pelo shell.
- [x] Disabled não altera output no escopo desta fase: o shell não participa do frame path.
- [x] Bootstrap/probe desacoplados de Acquire/Execute.
- [x] Build files tocados <=300 sem multiplicar targets/jobs existentes.

## Próxima fase

Fase 03 — somente após instrução explícita do usuário.
