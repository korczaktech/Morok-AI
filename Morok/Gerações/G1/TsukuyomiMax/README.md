# Tsukuyomi Max

Modelo neural unificado da geração G1. Mantém um único conjunto de parâmetros e um checkpoint próprio para aprender e responder aos diferentes tipos de pergunta e diálogos da G1.

O modelo usa o motor compartilhado de G1; sua identidade de modelo, política de geração e checkpoint são próprios. A seleção deste modelo no workflow executa um único ciclo de treinamento e avaliação com todas as perguntas selecionadas, sem dividir a rede por perfil.

A política de inferência busca equilíbrio entre diversidade, continuidade e tamanho da resposta. Seus parâmetros de geração não adicionam pesos treináveis.
