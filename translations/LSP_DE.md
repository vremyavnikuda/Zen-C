# Zen C Language Server Protocol (LSP)

Zen C verfügt über einen integrierten Language Server (LSP), der Editorfunktionen wie Autovervollständigung, Springen zur Definition und Fehlerdiagnose bereitstellt.

## Server starten

Der Sprachserver ist in den `zc`-Compiler integriert. Du kannst ihn manuell starten (obwohl dies normalerweise von deinem Editor übernommen wird):

```bash
zc lsp
```

Die Kommunikation erfolgt über die Standardeingabe/-ausgabe (stdio).

## Editor-Konfiguration

### VS Code

Verwende für Visual Studio Code die offizielle Zen C-Erweiterung:

- **Repository**: [zenc-lang/vscode-zenc](https://github.com/zenc-lang/vscode-zenc)

Installiere die Erweiterung direkt vom **[Visual Studio Code Marketplace](https://marketplace.visualstudio.com/items?itemName=Z-libs.zenc)**.

Alternativ kannst du die `.vsix`-Datei aus dem Quellcode erstellen.

### Vim / Neovim

Vim und Neovim werden durch das Plugin `zenc.vim` unterstützt, das Syntaxhervorhebung und Hilfsfunktionen zur LSP-Konfiguration bietet.

- **Repository**: [zenc-lang/zenc.vim](https://github.com/zenc-lang/zenc.vim)

#### Neovim (Beispiel mit `lazy.nvim`)

Wenn du `nvim-lspconfig` verwendest, kannst du `zc` als benutzerdefinierten Server registrieren:

```lua
local lspconfig = require('lspconfig')
local configs = require('lspconfig.configs')

if not configs.zenc then
  configs.zenc = {
    default_config = {
      cmd = { 'zc', 'lsp' },
      filetypes = { 'zenc', 'zc' },
      root_dir = lspconfig.util.root_pattern('.git', 'build.bat', 'Makefile'),
      settings = {},
    },
  }
end

lspconfig.zenc.setup {}
```

### Zed

Um Zen C in Zed zu konfigurieren, füge Folgendes zu deiner `settings.json`-Datei oder deiner Sprachkonfiguration hinzu:

```json
{
  "lsp": {
    "zenc": {
      "binary": {
        "path": "zc",
        "arguments": ["lsp"]
      }
    }
  },
  "languages": {
    "Zen C": {
      "language_servers": ["zenc"]
    }
  }
}
```

### Generische Editoren (Sublime Text, Emacs usw.)

Für jeden Editor, der generische LSP-Clients unterstützt:

1. **Befehl**: `zc`
2. **Argumente**: `lsp`
3. **Transport**: `stdio`
4. **Dateierweiterungen**: `.zc`

## Funktionen

- **Diagnose**: Syntax- und Typfehler in Echtzeit.
- **Zur Definition springen**: Direkt zu den Definitionen von Strukturen, Funktionen und Variablen springen.
- **Autovervollständigung**: Kontextbezogene Vorschläge für Felder und Methoden.
- **Hover**: Typinformationen und Dokumentation beim Überfahren mit der Maus.