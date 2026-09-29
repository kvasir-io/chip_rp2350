"""RP2350 commands for Kvasir_SDK/tools/kvasir_bench.py: `kvasir_bench.py chip B T <name>`."""
import time

QMI_WIDTHS = {0: "single", 1: "dual", 2: "quad"}


def field(value: int, fields: dict[str, tuple[int, int]], name: str) -> int:
    lsb, width = fields[name]
    return (value >> lsb) & ((1 << width) - 1)


def qmi_read_mode(timing: int, rfmt: int, rcmd: int, t: dict, f: dict) -> list[str]:
    """The QMI's XIP read timing and frame, in words."""
    def w(name): return QMI_WIDTHS.get(field(rfmt, f, name), "?")
    prefix = field(rfmt, f, "PREFIX_LEN") != 0
    suffix_bits = 4 * field(rfmt, f, "SUFFIX_LEN")
    dummy_bits = 4 * field(rfmt, f, "DUMMY_LEN")
    # no prefix but a suffix: the flash's continuous read mode (RP2350 datasheet 12.14.2)
    return [(f"clkdiv {field(timing, t, 'CLKDIV')}, rxdelay {field(timing, t, 'RXDELAY')}, "
             f"cooldown {field(timing, t, 'COOLDOWN')}, max_select {field(timing, t, 'MAX_SELECT')}"),
            (f"command {rcmd & 0xFF:02X}h ({w('PREFIX_WIDTH')}) on every burst" if prefix
             else "no command prefix" + (" (continuous read)" if suffix_bits else ""))
            + f", address {w('ADDR_WIDTH')}"
            + (f", {suffix_bits}-bit mode byte {(rcmd >> 8) & 0xFF:02X}h ({w('SUFFIX_WIDTH')})" if suffix_bits else "")
            + (f", {dummy_bits} dummy bits ({w('DUMMY_WIDTH')})" if dummy_bits else "")
            + f", data {w('DATA_WIDTH')}" + (", DTR" if field(rfmt, f, "DTR") else "")]


def xip_arguments(parser) -> None:
    parser.add_argument("--seconds", type=float, default=2)
    parser.add_argument("--loop-us", type=float, default=0,
                        help="the main loop's pass time: also print the misses per pass")


def xip(bench, args) -> None:
    xbase, xregs = bench.svd("XIP_CTRL")
    qbase, qregs = bench.svd("QMI")
    hit_at, acc_at = xbase + xregs["CTR_HIT"][0], xbase + xregs["CTR_ACC"][0]
    tim, rfmt, rcmd = (qbase + qregs[n][0]
                       for n in ("M0_TIMING", "M0_RFMT", "M0_RCMD"))
    # saturating and write-to-clear (RP2350 datasheet Tables 442/443): full within a minute of
    # boot, so clear them first. CTR_ACC counts uncached accesses too (Table 443).
    bench.write([(hit_at, 0), (acc_at, 0)])
    t0, (h0, a0) = bench.read([hit_at, acc_at])
    time.sleep(args.seconds)
    t1, (h1, a1, timing, fmt, cmd) = bench.read(
        [hit_at, acc_at, tim, rfmt, rcmd])
    if 0xFFFFFFFF in (h1, a1):
        bench.die(
            f"the XIP counters saturated within {args.seconds:g} s: take a shorter --seconds")
    seconds = (t1 - t0) / 1e6
    hits, accesses = h1 - h0, a1 - a0
    other = accesses - hits
    print(f"XIP over {seconds:.2f} s: {accesses / seconds:,.0f} accesses/s, "
          f"{100 * hits / accesses if accesses else 100:.2f} % from the cache, "
          f"{other / seconds:,.0f}/s not from the cache (misses + uncached accesses)")
    if args.loop_us:
        print(f"  {other / seconds * args.loop_us / 1e6:,.0f} not from the cache per main-loop pass "
              f"(--loop-us {args.loop_us:g})")
    for line in qmi_read_mode(timing, fmt, cmd, qregs["M0_TIMING"][1], qregs["M0_RFMT"][1]):
        print("  flash: " + line)


def commands() -> dict:
    return {"xip": ("XIP cache hits/misses per second and the QMI's flash read mode",
                    xip_arguments, xip)}
