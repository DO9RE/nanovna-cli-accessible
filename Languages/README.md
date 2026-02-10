# Translation File Format

This directory contains language files for the NanoVNA CLI Accessible application.

## File Format

Translation files use the `.lng` extension and follow this format:

### Basic Key-Value Pairs

Simple translations without special characters:
```
KEY_NAME=Value text
```

### Quoted Multi-line Values (Recommended)

For multi-line texts or values containing special characters, use quoted format:
```
KEY_NAME="Line 1
Line 2
Line 3"
```

### Escape Sequences

Within quoted strings, the following escape sequences are supported:

- `\"` - Double quote character
- `\\` - Backslash character
- `\n` - Newline (alternative to literal line break)
- `\t` - Tab character
- `\r` - Carriage return

Example with escape sequences:
```
KEY_WITH_QUOTES="This text contains \"quoted\" words"
KEY_WITH_ESCAPES="Tab:\tNewline:\nBackslash:\\"
```

### Backward Compatibility

Unquoted values also support escape sequences (for backward compatibility):
```
OLD_STYLE_KEY=This has a newline:\nAnd a tab:\t
```

However, quoted multi-line format is preferred for readability.

## Comments

Lines starting with `#` are treated as comments:
```
# This is a comment
KEY=Value
```

## Available Languages

- `eng.lng` - English
- `deu.lng` - German (Deutsch)

## Adding New Languages

1. Copy an existing `.lng` file
2. Translate all values (keep keys unchanged)
3. Update the first comment line with language name:
   ```
   # LANGUAGE_NAME=YourLanguageName
   ```
4. The application will automatically detect the new language file

## Best Practices

1. **Use quoted multi-line format** for help texts and long messages
2. **Escape quotes** when they appear in text: `\"quoted\"`
3. **Keep keys consistent** across all language files
4. **Test the translation** by loading it in the application
5. **Use literal line breaks** in quoted strings instead of `\n` for better readability

## Example: Multi-line Help Text

```
HELP_MENU="
=== Menu Help ===

Commands:
  H - Help    Show this help
  Q - Quit    Exit the program

Tips:
  - Use \"H\" key for help
  - Press \"ESC\" to go back
"
```

This is much more maintainable than:

```
HELP_MENU_TITLE=== Menu Help ===
HELP_MENU_CMD1=  H - Help    Show this help
HELP_MENU_CMD2=  Q - Quit    Exit the program
HELP_MENU_TIPS=Tips:\n  - Use "H" key for help\n  - Press "ESC" to go back
```
