# 10 — PyTorch с CUDA в rootless glibc-джейле (ступень 4)

Дата: 2026-11-15. Машина: (gpu) 192.168.0.86, без root. Инструмент:
`tools/nv-jail.sh` (setup | torch | run), джейл `~/nv-jail`.

## Результат

```
torch 2.15.0+cu132
cuda available: True
devices: 5
name: NVIDIA GeForce RTX 4060 Ti
matmul on GPU ok: (1024, 1024) True
```

`torch.rand(1024,1024, device="cuda")` + матричное умножение +
`cuda.synchronize()` — первый реальный compute под управлением стека
«джейл + драйзер хоста». Ступень 4 лестницы закрыта.

## Состав джейла

- ubuntu-base 24.04.5 (glibc 2.39, Python 3.12.3) — tar с cdimage;
- bwrap: `--unshare-user --uid 0`, `/proc`, `/dev` свежий, `/tmp` — bind из
  `$JAIL/tmp` (диск!), `--ro-bind` драйверов хоста `libcuda.so.580.159.03`
  и `libnvidia-ml.so.580.159.03` как `.so.1`, ноды `/dev/nvidia{0..4}`,
  `nvidiactl`, `nvidia-uvm`;
- venv + `pip install --no-cache-dir torch` (официальные manylinux-колёса,
  cu132: cublas/cudnn/cusolver/cuda-bindings).

## Ямы, в которые мы наступили (все — rootless-специфика)

1. **dpkg не может ставить пакеты в userns с одним uid**: `chown root:root`
   → EINVAL, потому что **gid 0 не замаплен** (bwrap 0.11 без newuidmap/
   newgidmap мапит только uid). Обход: `apt-get -d` (download-only) +
   ручная распаковка `ar x` + `tar -xf data.tar.zst --no-same-owner`.
   Postinst'ы не запускаются; вручную сгенерирован
   `/etc/ca-certificates.conf` и вызван `update-ca-certificates`
   (без него pip: «invalid path /etc/ssl/certs/ca-certificates.crt»).
2. **apt http-метод** падает на `setgroups 65534` →
   `-o APT::Sandbox::User=root`; плюс `-o APT::Immediate-Configure=0`,
   и подчищать `/var/lib/dpkg/status` от полураспакованных записей.
3. **`--tmpfs /tmp` = RAM**: хост и так 46/47 GiB (ollama), pip сожрал бы
   своп и умер. Скретч пишем на диск: `--dev-bind $JAIL/tmp /tmp`.
4. **Диск 98%**: установка влезла только после удаления чистых кэшей
   (`~/.cache/pip`, `~/.cache/uv`). Floor 2 GiB выдержан: до и после —
   2.4 GiB free. На хосте обнаружен параллельный `~/nvjail` (5.8 GiB,
   чужая копия джейла) — не трогали.

## Что это доказывает для имиджа

Официальные CUDA-колёса PyTorch работают в минимальном glibc-джейле поверх
musl-хоста с драйвером хоста — то есть план report/04 (glibc-jail как
носитель PyTorch) годен без патчей, контейнерных демонов и root. Нативный
musl-torch остаётся отдельной (stretch) ступенью.
