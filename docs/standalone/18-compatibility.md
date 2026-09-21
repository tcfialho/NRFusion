# Fase 18 — State restoration e compatibilidade

## Objetivo

Compatibilidade difícil isolada, sem imposto global.

## Dependências

Carriers principais.

## Fora de escopo

- Copiar D3D12_Hooks inteiro
- Tracking pesado default

## Checklist de implementação

- [ ] Identificar root signature/descriptor heap/PSO restore reais.
- [ ] Capturar mínimo.
- [ ] Capability/profile gate.
- [ ] Evitar maps globais default.
- [ ] Preservar bindless.
- [ ] Documentar exceções.

## Revisão obrigatória

- [ ] Cada state item com razão concreta.
- [ ] Locks/maps fora do default fast path.
- [ ] Early-return restore.
- [ ] Profile não mascara bug geral.

## Validação rápida

- [ ] Harness compatibility mode.
- [ ] Benchmark default vs compatibility.
- [ ] Failure durante restore.

## Gate de conclusão

- [ ] Default sem custo desnecessário.
- [ ] Casos conhecidos preservados.

## Entregáveis

- Compatibility layer
- Profiles

## Próxima fase

Fase 19.
