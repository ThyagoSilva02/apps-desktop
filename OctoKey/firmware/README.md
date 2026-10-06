# Firmware MacroPill — oito botões

Firmware USB serial para **STM32F103C8/CB**, Blue Pill com cristal de **8 MHz**, 64 KB de flash e 20 KB de RAM. Não use este arquivo em outro modelo sem adaptar o código. A placa conectada foi identificada pela família STM32F1 Medium Density e informa 128 KB de flash; o firmware ocupa somente 6.736 bytes e usa o limite de 64 KB.

**Estado atual: firmware GRAVADO e conferido por leitura na STM32.** A conexão SWD identificou Cortex-M3 r1p1, DPIDR `0x1ba01477`, dispositivo `0x00006410`, família STM32F1 Medium Density, com 128 KB de flash. Os 6.736 bytes do firmware lidos da placa são idênticos ao arquivo fornecido. A USB serial foi reconhecida pelo Windows como **COM3** e testada a 115200, 8N1, com DTR. Foram recebidos `BTN1` até `BTN8` por USB, simulando temporariamente as entradas PA0 a PA7 com seus resistores internos; a configuração foi restaurada. Os botões físicos ainda precisam ser montados e testados.

## Conectar os oito botões

Cada botão fecha o contato entre seu pino e **GND**. Os resistores de pull-up são internos; não é necessário resistor externo para esses botões.

| Botão | Pino da placa | Mensagem |
|---|---|---|
| 1 | A0 / PA0 | BTN1 |
| 2 | A1 / PA1 | BTN2 |
| 3 | A2 / PA2 | BTN3 |
| 4 | A3 / PA3 | BTN4 |
| 5 | A4 / PA4 | BTN5 |
| 6 | A5 / PA5 | BTN6 |
| 7 | A6 / PA6 | BTN7 |
| 8 | A7 / PA7 | BTN8 |

Nos botões tácteis de quatro pernas existem dois pares de terminais já unidos internamente. Use um terminal de cada par, de modo que o contato só feche ao pressionar; um multímetro em continuidade ajuda a identificar os pares.

PA11 e PA12 ficam reservados ao USB; PA13 e PA14 ficam reservados à gravação SWD. O firmware não desativa o acesso SWD.

## Gravação pelo ST-Link

| ST-Link | Blue Pill |
|---|---|
| GND | G / GND |
| SWDIO | DIO / PA13 |
| SWCLK | CLK / PA14 |
| NRST, se disponível | R / RESET, opcional |

Identifique os pinos pelos nomes impressos no seu gravador; a posição deles varia entre modelos. Para as instruções acima, alimente a Blue Pill pelo cabo micro-USB e use GND, SWDIO e SWCLK do gravador. Não ligue 5 V aos pinos de sinais ou ao pino 3.3 V.

Antes de gravar, confirme a identificação e o tamanho da flash por SWD e faça uma cópia da memória existente. Caso a placa esteja protegida, não execute desbloqueio nem apagamento total sem avaliar a perda dos dados existentes.

O arquivo `MacroPill.bin` começa no endereço **0x08000000**. O arquivo `MacroPill.hex` já contém os endereços. A gravação substitui o programa/bootloader presente nessa região. Confira a gravação por leitura antes de iniciar.

Depois da gravação, coloque **BOOT0 em 0 e BOOT1 em 0** e aperte RESET. Ligue o micro-USB da Blue Pill ao PC para a comunicação com o MacroPill Control. O ST-Link é apenas o gravador; ele não é a porta COM do macropad.

## Funcionamento

- USB CDC ACM, compatível com o driver serial nativo do Windows 10/11.
- Identificador USB do exemplo CDC STM32: 0483:5740; número de série derivado do identificador único do chip.
- Uma mensagem ASCII `BTNn\n` por aperto estável durante 25 ms.
- Manter um botão apertado não repete o comando.
- Botões simultâneos são independentes; a fila comporta 31 eventos.
- Eventos são enviados somente quando a porta serial está aberta com DTR ativado, como faz o MacroPill Control.
- Botões apertados ao ligar a placa só passam a disparar depois de soltá-los e apertá-los novamente.
- LED PC13 pisca enquanto aguarda; fica aceso com a porta serial aberta nas placas com LED ativo em nível baixo.

No MacroPill Control: atualizar portas, selecionar a COM criada pela placa, escolher 115200 e conectar. A taxa de transmissão é informativa na USB CDC, mas é aceita pelo firmware. Configure as oito ações e salve.

## Arquivos e compilação

- `main.c`: USB serial, entradas e envio das mensagens.
- `buttons.h`: filtro de contato dos botões.
- `startup.c`, `runtime.c`, `stm32f103.ld`: inicialização e memória.
- `MacroPill.bin`, `MacroPill.hex`: firmware compilado para gravação. O arquivo ELF é gerado localmente ao recompilar.
- `test_buttons.c`: teste de contato, pressão contínua, soltura, botão apertado ao ligar, retorno do contador e oito botões simultâneos.
- `third_party/libopencm3`: fontes e cabeçalhos da biblioteca USB/periféricos.

Para recompilar com [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads), use PowerShell na pasta do firmware:

```powershell
.\build-gcc.ps1 'C:\caminho\arm-gnu-toolchain\bin'
```

O BIN fornecido foi compilado com Clang 23.1.2 para Cortex-M3 e LLD 22.0.0. O caminho GCC acima é uma alternativa de recompilação e não foi executado neste PC. Foram conferidos o limite de memória, a tabela de vetores e os descritores CDC. O teste nativo dos botões passou para contato instável, pressão contínua, soltura, botão apertado ao ligar, retorno do contador e oito botões simultâneos. A gravação e a transmissão dos oito comandos pela USB física foram verificadas. Falta verificar os botões físicos quando estiverem montados. Registros e backups da máquina de desenvolvimento não são distribuídos neste repositório.

## Licença e referências

Este firmware e sua adaptação do exemplo CDC usam **LGPL-3.0-or-later**. O cabeçalho original de Gareth McMullin está preservado em `main.c`. As licenças da biblioteca acompanham suas fontes. O aplicativo desktop é um projeto separado.

- [libopencm3 e biblioteca USB](https://github.com/libopencm3/libopencm3)
- [Exemplo CDC original](https://github.com/libopencm3/libopencm3-examples/tree/master/examples/stm32/f1/stm32-h103/usb_cdcacm)
- [Driver serial nativo do Windows](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-driver-installation-based-on-compatible-ids)
- [Driver oficial ST-Link](https://www.st.com/en/development-tools/stsw-link009.html)
