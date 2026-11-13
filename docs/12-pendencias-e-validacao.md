# 12. Pendências e plano de validação

Este documento diferencia decisões conceituais aprovadas de detalhes ainda não especificados.

## 12.1 Pendências matemáticas da RLAH

- Definição operacional de perda preditiva.
- Definição de custo de compressão, incluindo metadados.
- Definição de perda de reconstrução.
- Valores e estratégia de λ e μ.
- Normalização dos quatro critérios.
- Estimador de frequência.
- Medida de ganho preditivo marginal.
- Medida de preservação de informação.
- Regra matemática de atualização dos pesos adaptativos.
- Limites e taxa de atualização dos pesos.
- Limiar de criação e descarte com histerese.

## 12.2 Pendências de arquitetura

- Dimensões concretas do núcleo recorrente.
- Equações de atualização do estado.
- Dimensão e formato das representações latentes.
- Estrutura de dados dos três níveis hierárquicos.
- Algoritmo de geração de candidatos.
- Orçamento máximo de candidatos por posição.
- Política de atualização de segmentação.
- Fatoração e ordem dos oito bits da saída.
- Dimensões definitivas que somem exatamente 1.000 parâmetros na G1.

## 12.3 Pendências de engenharia

- Organização final dos diretórios C++.
- Sistema de build e compilador de referência.
- Biblioteca numérica, se necessária.
- Formato de checkpoint.
- Formato dos corpora e carregamento incremental.
- Estratégia de testes unitários e integração.
- Ferramentas de benchmark e perfil de memória.

## 12.4 Revisão bibliográfica

Comparar a proposta com abordagens de modelagem em bytes, tokenização aprendida, segmentação adaptativa, compressão e modelos recorrentes. Trabalhos de referência iniciais incluem ByT5, Charformer e CANINE. A revisão deverá procurar também trabalhos posteriores e resultados recentes antes de qualquer afirmação de novidade.

## 12.5 Plano de validação

1. Formalizar matematicamente cada termo.
2. Criar um baseline de predição de bytes.
3. Implementar uma versão mínima sem segmentação adaptativa.
4. Medir qualidade e desempenho.
5. Adicionar segmentação e medir ganho marginal.
6. Adicionar hierarquia e medir ganho marginal.
7. Adicionar controlador adaptativo e medir custo/benefício.
8. Verificar reconstrução sem perdas.
9. Validar contagem exata de parâmetros.
10. Executar treinamento reproduzível dentro do orçamento de RAM.

## 12.6 Regra de status

- **Aprovado:** decisão de alto nível escolhida.
- **Especificado:** detalhes suficientes para implementar sem inventar regras.
- **Implementado:** código existente.
- **Testado:** testes executados com resultado registrado.
- **Validado:** resultado comparado e aceito com evidência.

Uma decisão aprovada não deve ser descrita como implementação concluída.
