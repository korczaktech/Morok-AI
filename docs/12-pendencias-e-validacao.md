# 12. Pendências e plano de validação

Este documento diferencia decisões conceituais aprovadas de detalhes ainda não especificados. A arquitetura matemática proposta está nos documentos 13–17.

## 12.1 Pendências matemáticas remanescentes

- Definir metas numéricas e sinal operacional d_i para atualização adaptativa dos pesos.
- Calibrar η e os limiares de criação, retenção e descarte.
- Definir frequência mínima e orçamento máximo de candidatos por bloco.
- Definir comprimento máximo das unidades de nível 2.
- Especificar a política de tratamento de UTF-8 inválido.
- Definir hiperparâmetros finais de treinamento.
- Validar a fatoração de bits contra softmax de 256 bytes.

## 12.2 Pendências de arquitetura

- Formalizar o formato de dados hierárquico e suas referências.
- Definir limites de memória e política de remoção do cache de candidatos.
- Confirmar dimensões e contagem com inventário automático no código.
- Avaliar se a dimensão d=11 oferece estabilidade suficiente.
- Definir o contrato de transporte de estado entre blocos e sessões.

## 12.3 Pendências de engenharia

- Organização final dos diretórios C++.
- Sistema de build e compilador de referência.
- Biblioteca numérica, se necessária.
- Formato de checkpoint.
- Formato dos corpora e carregamento incremental.
- Estratégia de testes unitários e integração.
- Ferramentas de benchmark e perfil de memória.

## 12.4 Revisão bibliográfica

A revisão inicial está em [Revisão bibliográfica e posicionamento](13-arquitetura-matematica-rlah.md) e, em detalhe, em [documento 17](17-revisao-bibliografica-e-posicionamento.md). Trabalhos relevantes incluem ByT5, Charformer, CANINE, RWKV, Mamba, Mamba-3, ByteFlow e zip2zip. A revisão deverá continuar antes de qualquer afirmação de novidade.

## 12.5 Plano de validação

1. Implementar baseline simples de predição de bytes.
2. Implementar núcleo fast/slow e comparar com RNN simples.
3. Validar gradientes e estabilidade numérica.
4. Verificar inventário exato de 1.000 parâmetros.
5. Implementar reconstrução hierárquica reversível.
6. Adicionar pontuador de agrupamentos e medir ganho marginal.
7. Adicionar controlador adaptativo e medir custo/benefício.
8. Comparar saída binária com softmax de 256 classes.
9. Executar ablações de hierarquia, segmentação e pesos adaptativos.
10. Medir BPB, latência, throughput, RAM e tempo de treinamento.
11. Repetir experimentos com sementes diferentes.
12. Avaliar dados fora do conjunto de treinamento.

## 12.6 Regra de status

- **Aprovado:** decisão de alto nível escolhida.
- **Especificado:** detalhes suficientes para implementar sem inventar regras.
- **Implementado:** código existente.
- **Testado:** testes executados com resultado registrado.
- **Validado:** resultado comparado e aceito com evidência.

Uma decisão aprovada não deve ser descrita como implementação concluída.
