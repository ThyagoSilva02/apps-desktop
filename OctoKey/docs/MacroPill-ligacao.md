# MacroPill: oito botões e LED externo de alimentação

Esquema compatível com o firmware já gravado na Blue Pill. O desenho usa os nomes dos pinos; a disposição física deles na placa é diferente. Siga as inscrições na placa.

A versão `MacroPill-esquema-completo-v2.png` mostra os fios de 3,3 V em vermelho ligados fisicamente aos oito resistores. Os arcos nas passagens pelo GND indicam fios separados, sem conexão. Pontos cheios indicam junções. Nunca una a linha vermelha de 3,3 V à linha de GND.

## Componentes

- 1 placa STM32 Blue Pill e cabo micro-USB com dados.
- 8 botões momentâneos normalmente abertos.
- 8 resistores de 10 kΩ, um por botão (opcionais porque o firmware já ativa os pull-ups internos).
- 1 LED indicador comum de duas pernas.
- 1 resistor de 220 Ω em série com o LED. Resistores de ¼ W servem neste circuito.
- Fios, protoboard ou placa de montagem.

## Botões e resistores de 10 kΩ

| Botão | Pino | Resistor externo | Outra perna do botão |
|---|---|---|---|
| 1 | A0 / PA0 | R1: 10 kΩ entre 3,3 V e A0 | GND |
| 2 | A1 / PA1 | R2: 10 kΩ entre 3,3 V e A1 | GND |
| 3 | A2 / PA2 | R3: 10 kΩ entre 3,3 V e A2 | GND |
| 4 | A3 / PA3 | R4: 10 kΩ entre 3,3 V e A3 | GND |
| 5 | A4 / PA4 | R5: 10 kΩ entre 3,3 V e A4 | GND |
| 6 | A5 / PA5 | R6: 10 kΩ entre 3,3 V e A5 | GND |
| 7 | A6 / PA6 | R7: 10 kΩ entre 3,3 V e A6 | GND |
| 8 | A7 / PA7 | R8: 10 kΩ entre 3,3 V e A7 | GND |

Exemplo do botão 1, repetido para A1–A7:

```text
3,3 V ── resistor 10 kΩ ──┬── A0
                         │
                       botão 1
                         │
                        GND
```

O resistor NÃO fica em série entre o botão e o GND. Ele é uma derivação entre a entrada e o 3,3 V. Não ligue o resistor de pull-up ao 5 V. Os resistores não têm polaridade.

Todos os pontos GND se ligam ao mesmo G/GND da placa. Todos os pontos 3,3 V se ligam ao mesmo pino 3,3 da placa. Numa protoboard, distribua esses dois sinais em linhas separadas; algumas linhas de alimentação são interrompidas no meio.

Nos botões comuns de quatro pernas, use duas pernas em cantos opostos, na diagonal. As outras duas ficam livres. Se houver um multímetro, confirme: sem apertar, não deve haver continuidade entre as duas pernas escolhidas; apertando, deve haver. Não ligue a entrada e o GND a dois contatos permanentemente unidos.

## LED externo com resistor de 220 Ω

```text
3,3 V da placa ── 220 Ω ── ânodo (+) LED cátodo (−) ── G/GND
```

Em LED indicador comum de duas pernas ainda não cortadas, a perna longa normalmente é o ânodo. A perna curta e o lado achatado da cápsula normalmente indicam o cátodo. Se o componente tiver outra marcação, siga seu datasheet.

O LED fica aceso enquanto houver alimentação de 3,3 V. Não depende do software desktop, dos botões nem de uma saída GPIO. Não é necessário alterar ou regravar o firmware para essa ligação. O LED original da placa permanece funcionando.

Para um LED com queda de tensão de aproximadamente 2 V, a corrente estimada é (3,3 − 2) / 220 ≈ 5,9 mA. Isso é uma estimativa para um LED indicador comum; o valor exato depende do modelo e da cor. Um LED azul/branco pode apresentar pouco brilho em 3,3 V. Não substitua por um LED de potência nem ligue LED sem resistor.

## Montagem e teste

1. Desconecte o USB e o ST-Link antes de montar ou soldar.
2. Solde os pinos de A0–A7, GND e 3,3 V se estiverem apenas encaixados nos furos.
3. Monte primeiro o botão 1, seu resistor de 10 kΩ e o LED com 220 Ω. Confira que não há curto entre 3,3 V e GND.
4. Deixe BOOT0 e BOOT1 em 0 e conecte somente o micro-USB ao PC para uso normal. O ST-Link pode ficar desconectado.
5. O LED externo deve acender. Abra MacroPill Control, atualize as portas, selecione a porta da STM32 (COM3 no último teste), velocidade 115200, e conecte. A porta pode mudar após trocar a conexão USB.
6. Teste o botão 1 com uma ação escolhida no programa. Depois monte os outros sete.

## Base técnica

O código atual configura PA0–PA7 como entradas com pull-up e usa PC13 somente para o LED já existente na placa. O STM32F103 tem pull-ups internos equivalentes a aproximadamente 30–50 kΩ: [datasheet da ST, tabela de características estáticas das entradas](https://www.st.com/resource/en/datasheet/stm32f103c8.pdf).

Como exemplo de LED vermelho indicador, o [datasheet Kingbright WP7113ID](https://www.kingbrightusa.com/images/catalog/SPEC/WP7113ID.pdf) especifica tensão direta típica próxima a 1,9 V a 10 mA. Isso não identifica o LED do usuário; o cálculo acima usa a hipótese de 2 V.

O esquema foi conferido contra o firmware e revisado visualmente. A montagem física com os resistores e o LED ainda precisa ser testada.
