# Fase 18 — State restoration e compatibilidade

## Objetivo

Preservar engines difíceis através de exceções mínimas e gated.

## Dependências

Carriers principais.

## Fora de escopo

- Copiar D3D12_Hooks inteiro
- Tracking global por padrão

## Implementação

- [ ] Identificar quais states realmente precisam restore por rota.
- [ ] Implementar captura mínima de root signature/heaps/PSO/root params somente quando necessária.
- [ ] Ativar por capability/profile observável, não por suposição.
- [ ] Evitar maps/mutexes globais no fast path default.
- [ ] Documentar game/API reason de cada exception.
- [ ] Garantir restore também em early-return/failure.

## Revisão obrigatória

- [ ] Compatibility flag não pode esconder bug geral.
- [ ] State capturado precisa de owner e lifetime.
- [ ] Nenhuma exceção pode impor custo aos games que não precisam.
- [ ] Profile desconhecido usa caminho conservador sem adivinhar internals.

## Validação rápida

- [ ] Harness com cenário que exige state restore.
- [ ] Benchmark default vs compatibility.
- [ ] Failure injection durante pass/restore.

## Gate

- [ ] Default não paga custo de restore ampliado.
- [ ] Casos conhecidos continuam cobertos por gates claros.

## Próxima fase

Fase 19.
