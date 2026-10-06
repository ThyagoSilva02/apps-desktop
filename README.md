# Aplicativos desktop

Projetos de aplicativos desktop, incluindo ferramentas e sistemas para Windows.

## OctoKey — MacroPill Control

**Transforme oito botões físicos em atalhos para o seu computador.** O OctoKey é um aplicativo para Windows que configura um macropad com STM32 Blue Pill conectado por USB. Você escolhe o que cada botão faz e pode mudar as ações pela interface.

Com ele, você pode:

- Abrir programas ou arquivos com um botão.
- Enviar atalhos de teclado, como `CTRL + SHIFT + M`.
- Abrir sites no navegador padrão.
- Salvar a configuração dos oito botões para usar novamente.
- Manter o aplicativo na bandeja do Windows durante o uso.

![Interface do OctoKey](OctoKey/docs/interface.png)

### Download e uso

**[Baixar OctoKey para Windows x64](https://github.com/ThyagoSilva02/apps-desktop/releases/tag/octokey-v1.0.0)**

Extraia o ZIP, abra `MacroPillControl.exe`, conecte a placa, selecione sua porta COM e configure as ações. O aplicativo requer **Windows 10 ou 11**, uma **STM32 com o firmware USB serial compatível** e um cabo USB de dados. Não é necessário instalar Python, .NET ou Qt.

- [Instruções completas e código-fonte](OctoKey/README.md)
- [Firmware e ligação dos botões](OctoKey/firmware/README.md)
- [Distribuição, hashes e assinatura digital](OctoKey/docs/distribution.md)

O download acompanha SHA-256 para conferir a integridade. O executável padrão ainda não possui assinatura digital; o Windows e os antivírus podem exibir avisos conforme suas verificações e a reputação do arquivo.
