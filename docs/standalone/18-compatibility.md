# Fase 18 — State restoration e compatibilidade

## Objetivo

Isolar compatibility exceptions em módulos pequenos por causa/estado, sem framework monolítico.

## Dependências

Carriers principais.

## Fora de escopo

- Copiar hooks genéricos do OptiScaler
- Tracking global default

## Implementação

- [ ] Identificar states realmente necessários por rota.
- [ ] Separar state capture/restore de profile matching/database.
- [ ] Capturar root signature/heaps/PSO/root params apenas quando necessário.
- [ ] Ativar por capability/profile observável.
- [ ] Evitar maps/mutexes globais no fast path.
- [ ] Documentar causa observável de cada exception.
- [ ] Se CompatibilityDatabase/ProfileStore oversized forem tocados, separar parsing/storage de policy/aplicação.
- [ ] Cada módulo <=300 linhas.

## Revisão obrigatória

- [ ] Profile não mascara bug geral.
- [ ] Restore cobre early-return/failure.
- [ ] Exceção não impõe custo global.
- [ ] Split por estado/ownership, não por game arbitrariamente.

## Validação rápida

- [ ] Harness state-restore.
- [ ] Benchmark default vs compatibility.
- [ ] Failure during restore.
- [ ] LOC checker.

## Gate

- [ ] Default sem overhead ampliado.
- [ ] Compatibility tocada respeita <=300 por arquivo.

## Próxima fase

Fase 19.
