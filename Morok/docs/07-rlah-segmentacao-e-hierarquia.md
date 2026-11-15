# 7. RLAH — Segmentação adaptativa e hierarquia

> **Atualização normativa:** este documento apresenta a decisão conceitual. Os comprimentos, estruturas, orçamentos, cache e regras de atualização da G1 estão definidos no documento 18, que prevalece em caso de divergência.

## 7.1 Unidades hierárquicas

A RLAH mantém múltiplas escalas simultaneamente, sem substituir permanentemente as unidades menores:
- nível 0: bytes;
- nível 1: sequências contíguas de 2 a 8 bytes;
- nível 2: combinações limitadas de unidades de nível 1.

A profundidade máxima inicial é de três níveis contando bytes.

## 7.2 Geração de candidatos

O sistema gera candidatos de janelas locais, sem materializar todos os substrings possíveis. Frequência mínima, orçamento máximo e limites de comprimento estão no documento 18.

## 7.3 Reconstrução

Uma unidade maior deverá poder ser expandida em unidades menores. O formato deverá preservar exatamente os bytes originais. Qualquer colisão de hash deverá ser resolvida pela comparação dos bytes.

## 7.4 Atualização periódica

A segmentação é reavaliada entre épocas e congelada durante cada época. Atualizações devem ser determinísticas para a mesma semente, dados e configuração.

## 7.5 Prevenção de patologias

- limitar tamanho para evitar agrupamento excessivo;
- exigir benefício líquido para evitar fragmentação;
- impor round-trip exato;
- usar histerese para evitar oscilação;
- impor orçamento de candidatos;
- incluir metadados no custo de compressão.

## 7.6 Limite de integração

Na G1, a hierarquia é subsistema auxiliar de representação e compressão; o núcleo preditivo ainda processa bytes individuais. Não alegar aceleração ou melhoria de BPB end-to-end. Integração dos agrupamentos ao preditor exige uma especificação de geração posterior.
