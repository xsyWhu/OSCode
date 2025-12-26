1.给脚本可执行权限
```bash
chmod +x scripts/clean_identifier_files.sh
```
2.做演练不删除
```bash
./scripts/clean_identifier_files.sh --dry-run
```
3.执行删除
```bash
./scripts/clean_identifier_files.sh
```
4.无提示直接删除
```bash
./scripts/clean_identifier_files.sh -y path/to/dir
```