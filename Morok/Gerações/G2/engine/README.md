# Motor interno da G2

Este diretório contém a implementação privada da geração 2. Não é uma biblioteca pública e não inclui componentes internos da G1.

A G2 mantém sua própria implementação de:
- parâmetros e inicialização;
- estado recorrente, codificação de bytes e forward pass;
- BPTT e gradientes;
- AdamW/SGD e estado do otimizador;
- checkpoints de treinamento e inferência;
- treinamento de corpus;
- decodificação com validação UTF-8 e parada EOS.

Os especialistas A e B da G2 são instâncias do tipo de parâmetro e do motor definidos aqui. Eles não chamam `G1Parameters`, `G1State`, `train_g1_corpus`, `generate_g1` ou os checkpoints da G1.

Dependências permitidas: infraestrutura neutra em `include/morok/common`, primitivas matemáticas genéricas em `include/morok/model/math` e `Morok::Core`. Dependências proibidas: `G1/`, símbolos `G1*` e headers privados da G1.

O CMake compila estes fontes somente no alvo `Morok::G2`. O teste `tests/scripts/test_generation_isolation.py` verifica a fronteira de dependências.
