# Requiem: comparador contra a referência da NVIDIA, em Direct3D 12

O original foi perdido na exclusão de 14/09/2026 e não existe em backup nenhum. Isto é uma
reconstrução. O que sobreviveu foi a interface, preservada nas linhas de contexto de três
diffs no log da sessão, e ela está honrada aqui:

    RequiemGame.exe --fixed-scene            sem deslocamento; quadros comparáveis entre execuções
    RequiemGame.exe --deterministic-motion   deslocamento segue o número do quadro
    RequiemGame.exe --reference-on           começa exibindo a referência com o efeito
    RequiemGame.exe --frames 600             sai depois de N quadros

TAB alterna entre a referência desligada e a ligada. INSERT abre o menu do OptiScaler.
ESC fecha.

## O que ele faz

Exibe o quadro de referência da NVIDIA **com a renderização neural desligada**, passa esse
quadro por quem responder como upscaler — o runtime da NVIDIA ou o proxy que o OptiScaler põe
no lugar — e deixa você alternar para a referência **com o efeito ligado** para ver o que
mudou. As duas imagens são 3840x2160 e vieram do pacote de referência da própria NVIDIA.

Isso também é o que faz os kernels neurais rodarem, que é o que o perfilador de kernel precisa
para ter o que medir.

O deslocamento lento sobre a imagem não é enfeite: um upscaler sem movimento nenhum toma um
caminho que nunca toma num jogo, e o passe neural seria julgado por um problema que ele não
enfrenta. `--fixed-scene` desliga isso quando o objetivo é comparar capturas.

## Para medir

O pacote sai pronto em `dist\RequiemGame\`, com o OptiScaler como `dxgi.dll` e o runtime ao
lado.

1. Rode `dist\RequiemGame\RequiemGame.exe`
2. INSERT abre o menu
3. Mode → Custom → **Measure NR kernels**
4. Deixe rodar meio minuto e aperte **Copy table**

Se o console disser `[NGX] nenhuma biblioteca NGX encontrada`, não há runtime ao lado do
executável e o passe neural não roda. A mensagem existe para esse caso não ser confundido com
"mediu e não achou nada".
