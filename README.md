# Logitech M280 Battery Level

**[Português](#português) · [English](#english)**

Um programa minúsculo para Windows que mostra a bateria do mouse Logitech M280 como um número na bandeja do sistema, ao lado do relógio.
A tiny Windows program that shows your Logitech M280 mouse battery as a number in the system tray, next to the clock.

---

## Português

### O que faz
- Mostra a **porcentagem da bateria** como um número colorido na bandeja: 🟢 acima de 50% · 🟡 de 21% a 50% · 🔴 20% ou menos.
- Ao passar o cursor no ícone, mostra a porcentagem e, se o mouse informar, a **tensão da pilha** (ex.: `1,42 V`).
- Avisa com uma notificação quando a bateria chega a **15% ou menos**.
- Menu do botão direito: **Atualizar agora**, **Iniciar com o Windows** e **Sair**.

### Leve de verdade
- Um `.exe` de cerca de **26 KB**, em Win32 puro. Não precisa instalar nada: nem .NET, nem Python, nem driver.
- Usa **cerca de 1–2 MB de RAM** e praticamente **0% de CPU**. Só acorda para ler a bateria a cada 5 minutos, ou a cada 30 s se o mouse estiver dormindo.
- Não precisa do SetPoint nem do Logi Options+.

### Instalação
1. Baixe o `m280bat.exe` na página de [**Releases**](../../releases/latest).
2. Coloque o arquivo numa pasta fixa, por exemplo `C:\Programas\m280bat\`, e dê dois cliques nele.
3. O Windows vai mostrar o aviso azul do **SmartScreen**, porque o executável não tem assinatura digital. Clique em **Mais informações** e depois em **Executar assim mesmo**.
4. Procure o número perto do relógio. Se não aparecer, clique na setinha **^** da bandeja e arraste o ícone para a barra.
5. Para abrir junto com o Windows, clique no ícone com o botão direito e marque **Iniciar com o Windows**.

Para desinstalar: desmarque **Iniciar com o Windows**, clique em **Sair** e apague o `.exe`.

> 🔒 **É seguro?** O `.exe` da página de Releases é compilado pelo GitHub Actions direto deste código-fonte (veja `.github/workflows/build.yml`), e o hash SHA-256 é publicado junto. Se preferir, compile você mesmo (veja abaixo).

### Ícones e mensagens
| Ícone / dica | Significado |
|---|---|
| `85` verde/amarelo/vermelho | Porcentagem da bateria |
| `(aprox.)` na dica | O mouse só informa níveis ou a tensão da pilha, e o programa estima a porcentagem |
| `?` + "Mouse sem resposta" | O mouse está dormindo ou desligado. Mexa nele; o programa tenta de novo em 30 s |
| `?` + "Receptor Logitech não encontrado" | O receptor USB não está conectado ou não foi reconhecido |

Dica: dê **dois cliques no ícone** para atualizar na hora.

### Limitações
- **Intensidade do sinal:** o mouse e o receptor não informam a intensidade do sinal (RSSI), então nenhum programa consegue mostrar isso em porcentagem.
- **Precisão:** a bateria é a que o próprio mouse informa. Com pilha AA, a estimativa feita a partir da tensão é aproximada.
- **Vários aparelhos Logitech sem fio:** o programa mostra o primeiro que responder. Feito para o M280, mas deve funcionar com outros mouses Logitech com receptor Nano ou Unifying (M185, M170, M235 etc.).

### Compilar a partir do código
**MSYS2 (Windows):**
```bash
pacman -S mingw-w64-x86_64-gcc make
make
```
**Linux (compilação cruzada):**
```bash
sudo apt install gcc-mingw-w64-x86-64
make CC=x86_64-w64-mingw32-gcc
```
Comando direto, sem `make`:
```bash
gcc -municode -mwindows -Os -s src/m280bat.c -o m280bat.exe -lhid -lsetupapi -lgdi32 -lshell32 -ladvapi32
```

### Como funciona
O programa fala **HID++**, o protocolo da Logitech, direto com o receptor USB, pela interface HID de fabricante (`usage page 0xFF00`):
- **HID++ 2.0:** consulta os recursos `0x1004` (Unified Battery), `0x1000` (Battery Status) e `0x1001` (Battery Voltage).
- **HID++ 1.0:** lê os registradores `0x0D` (carga em %) e `0x07` (nível da bateria).

A leitura roda numa thread separada, abre o receptor, faz a consulta e fecha tudo em seguida. Entre uma leitura e outra, o programa não mantém nada aberto.

### Problemas comuns
- **Fica sempre em `?`:** mexa o mouse e dê dois cliques no ícone. Se continuar, feche o SetPoint ou o Logi Options+ e tente de novo.
- **O ícone sumiu depois que o Explorer reiniciou:** o programa recoloca o ícone sozinho. Se não voltar, abra o `.exe` de novo.
- **Achou um bug?** Abra uma [issue](../../issues) com a versão do Windows, o modelo do mouse e o texto que aparece ao passar o cursor no ícone.

---

## English

### What it does
- Shows the **battery percentage** as a colored number in the tray: 🟢 above 50% · 🟡 21–50% · 🔴 20% or less.
- Hovering the icon shows the percentage and, if the mouse reports it, the **battery voltage** (e.g. `1.42 V`).
- Shows a notification when the battery reaches **15% or less**.
- Right-click menu: **Atualizar agora** (refresh now), **Iniciar com o Windows** (start with Windows) and **Sair** (exit).

### Truly lightweight
- A single **~26 KB** `.exe` in plain Win32. Nothing to install: no .NET, no Python, no drivers.
- Uses **about 1–2 MB of RAM** and practically **0% CPU**. It only wakes up to read the battery every 5 minutes, or every 30 s while the mouse is asleep.
- Doesn't need SetPoint or Logi Options+.

### Installation
1. Download `m280bat.exe` from [**Releases**](../../releases/latest).
2. Put the file in a permanent folder, e.g. `C:\Programs\m280bat\`, and double-click it.
3. Windows will show the blue **SmartScreen** warning because the executable isn't digitally signed. Click **More info** and then **Run anyway**.
4. Look for the number near the clock. If it's not there, click the **^** arrow in the tray and drag the icon onto the taskbar.
5. To start it with Windows, right-click the icon and check **Iniciar com o Windows**.

To uninstall: uncheck **Iniciar com o Windows**, click **Sair** and delete the `.exe`.

> 🔒 **Is it safe?** The `.exe` on the Releases page is built by GitHub Actions straight from this source code (see `.github/workflows/build.yml`), and its SHA-256 hash is published alongside it. You can also build it yourself (see below).

### Icons and messages
The app's interface text is in Portuguese.

| Icon / tooltip | Meaning |
|---|---|
| green/yellow/red `85` | Battery percentage |
| `(aprox.)` in the tooltip | The mouse only reports levels or voltage, so the app estimates the percentage |
| `?` + "Mouse sem resposta" | The mouse is asleep or off. Move it; the app retries in 30 s |
| `?` + "Receptor Logitech não encontrado" | The USB receiver isn't plugged in or wasn't recognized |

Tip: **double-click the icon** to refresh immediately.

### Limitations
- **Signal strength:** neither the mouse nor the receiver reports signal strength (RSSI), so no program can show it as a percentage.
- **Accuracy:** the battery value is whatever the mouse itself reports. With an AA battery, the voltage-based estimate is approximate.
- **Multiple Logitech wireless devices:** the app shows the first one that answers. Built for the M280, but it should also work with other Logitech mice that use a Nano or Unifying receiver (M185, M170, M235, etc.).

### Building from source
**MSYS2 (Windows):**
```bash
pacman -S mingw-w64-x86_64-gcc make
make
```
**Linux (cross-compiling):**
```bash
sudo apt install gcc-mingw-w64-x86-64
make CC=x86_64-w64-mingw32-gcc
```
Direct command, without `make`:
```bash
gcc -municode -mwindows -Os -s src/m280bat.c -o m280bat.exe -lhid -lsetupapi -lgdi32 -lshell32 -ladvapi32
```

### How it works
The app talks **HID++**, Logitech's protocol, directly to the USB receiver through its vendor HID interface (`usage page 0xFF00`):
- **HID++ 2.0:** queries features `0x1004` (Unified Battery), `0x1000` (Battery Status) and `0x1001` (Battery Voltage).
- **HID++ 1.0:** reads registers `0x0D` (charge %) and `0x07` (battery level).

Each reading runs on a separate thread that opens the receiver, sends the query and closes everything right away. Between readings, the app keeps nothing open.

### Troubleshooting
- **Always shows `?`:** move the mouse and double-click the icon. If it keeps happening, close SetPoint or Logi Options+ and try again.
- **The icon disappeared after Explorer restarted:** the app re-adds the icon on its own. If it doesn't come back, run the `.exe` again.
- **Found a bug?** Open an [issue](../../issues) with your Windows version, mouse model and the tooltip text.

---

Não é afiliado à Logitech. "Logitech" é marca registrada da Logitech International S.A.
Not affiliated with Logitech. "Logitech" is a trademark of Logitech International S.A.

Licença / License: [MIT](LICENSE)
