# Execucao pelo GitHub Actions: entrada.txt -> saida.txt

## Fluxo

1. Uma alteracao em `entrada.txt` na branch `main` inicia o workflow `Morok AI - completar entrada.txt`.
2. O runner Ubuntu compila o executavel experimental `morok_complete`.
3. O programa le no maximo 4 KiB de entrada, inicializa os 1.000 parametros com uma semente derivada do proprio texto e realiza 50 epocas de atualizacao BPTT/AdamW sobre a sequencia fornecida.
4. Depois do treino, condiciona a inferencia ao texto de entrada e escolhe a cada passo o byte imprimivel ASCII com menor perda condicional.
5. Grava 160 bytes de continuacao em `saida.txt`.
6. O workflow faz commit direto na `main`, sem abrir PR.

## Limites importantes

- Este e um experimento de treinamento em uma unica sequencia, nao o pipeline de corpus da Etapa 8. Nao ha conjunto independente de validacao/teste.
- Os parametros comecam do zero em toda execucao. O aprendizado nao e persistido entre execucoes; o texto e reaprendido a cada alteracao.
- A G1 e extremamente pequena (1.000 parametros). A continuacao pode ser repetitiva ou sem sentido; o workflow nao implica que o modelo ja tenha capacidade linguistica.
- A saida e limitada a ASCII imprimivel, portanto nao produz acentos ou caracteres nao ASCII nesta primeira versao.
- A geracao tem comprimento fixo de 160 bytes; ainda nao existe token EOS.
- O workflow precisa de permissao de escrita de conteudo habilitada para o token GITHUB_TOKEN nas configuracoes de Actions do repositorio.
- O workflow so e disparado por push que altere `entrada.txt` na `main`. Execucoes manuais tambem estao disponiveis em Actions.

## Uso

Edite `entrada.txt` com pelo menos dois bytes e envie a alteracao para `main`. Acompanhe a execucao em Actions. Quando terminar, `saida.txt` sera atualizado por um commit do bot.
