# Sample Database

This folder contains a small sample database produced by the Tharavu engine.

Sample files:

- `sample/data/demo/users.odat` — sample tabular data (ODAT)
- `sample/data/demo/vocab.ovoc` — sample vocabulary table (OVOC)
- `sample/data/demo/embeddings.ovec` — sample embeddings file (OVEC)

## Regenerate the sample data

Compile and run the generator from the repository root:

```bash
gcc -std=c99 -I./include sample/create_sample_db.c src/data_engine.c src/platform.c -o sample/create_sample_db
sample/create_sample_db
```

## Example config

```ini
[paths]
data_dir = ./sample/data

[engine]
dim = 256
hash_cap = 131072
```
