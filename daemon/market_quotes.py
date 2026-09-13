"""Market tables for the device: crypto (CoinGecko) and B3 stocks (Yahoo Finance
prices + Fundamentus P/VP).

A table goes out as several small BLE writes, each {"<key>": rows, "o": offset,
"n": total}, packed under WRITE_BUDGET bytes. Rows are [cell, ..., change %]:
string cells pre-formatted pt-BR so the firmware only draws strings, then the
change as the last element.
"""

import asyncio
import html
import json
import re

import httpx

WRITE_BUDGET = 150

COINS = (
    ("bitcoin", "BTC"),
    ("ethereum", "ETH"),
    ("binancecoin", "BNB"),
    ("solana", "SOL"),
    ("ripple", "XRP"),
    ("cardano", "ADA"),
    ("dogecoin", "DOGE"),
    ("litecoin", "LTC"),
)
COINGECKO_URL = "https://api.coingecko.com/api/v3/simple/price"
CRYPTO_REFRESH_S = 120

# The device list scrolls, so the list can grow freely (QUOTE_TABLE_ROWS on the
# firmware). ROXO34 is Nubank's BRL-quoted BDR on B3.
STOCKS = (
    ("PETR4", "Petrobras"),
    ("VALE3", "Vale"),
    ("ITUB4", "Itaú"),
    ("BBDC4", "Bradesco"),
    ("BBAS3", "BB"),
    ("ROXO34", "Nubank"),
    ("ABEV3", "Ambev"),
    ("WEGE3", "WEG"),
    ("B3SA3", "B3"),
    ("KLBN11", "Klabin"),
    ("TAEE11", "Taesa"),
    ("POMO4", "Marcopolo"),
    ("BHIA3", "Casas Bahia"),
    ("GGBR4", "Gerdau"),
    ("MGLU3", "Magalu"),
    ("ISAE4", "ISA Energia"),
    ("CEAB3", "C&A"),
    ("COGN3", "Cogna"),
    ("AXIA3", "Axia"),
    ("SUZB3", "Suzano"),
    ("RENT3", "Localiza"),
    ("RDOR3", "Rede D'Or"),
    ("EQTL3", "Equatorial"),
    ("PRIO3", "PRIO"),
    ("RADL3", "RD Saúde"),
    ("SBSP3", "Sabesp"),
    ("EMBJ3", "Embraer"),
    ("VIVT3", "Vivo"),
    ("TIMS3", "TIM"),
    ("UGPA3", "Ultrapar"),
    ("LREN3", "Renner"),
    ("CMIG4", "Cemig"),
    ("CPLE3", "Copel"),
    ("ENEV3", "Eneva"),
    ("BBSE3", "BB Seguros"),
    ("SANB11", "Santander"),
    ("BPAC11", "BTG Pactual"),
    ("VBBR3", "Vibra"),
)

# Brazilian real-estate funds (FIIs) for their own screen, same row format.
FIIS = (
    ("MXRF11", "Maxi"),
    ("HGLG11", "CSHG Log"),
    ("KNRI11", "Kinea RI"),
    ("XPML11", "XP Malls"),
    ("VISC11", "Vinci SC"),
    ("HGRU11", "Pátria RU"),
    ("BTLG11", "BTG Log"),
    ("KNCR11", "Kinea CRI"),
    ("KNIP11", "Kinea IP"),
    ("CPTS11", "Capitânia"),
    ("IRDM11", "Iridium"),
    ("HGRE11", "CSHG RE"),
    ("XPLG11", "XP Log"),
    ("VILG11", "Vinci Log"),
    ("BRCO11", "Bresco"),
    ("RECR11", "REC CRI"),
    ("TRXF11", "TRX"),
    ("HSML11", "HSI Malls"),
    ("VGIR11", "Valora"),
    ("RBRR11", "RBR CRI"),
    ("RBRF11", "RBR Alpha"),
    ("JSRE11", "JS RE"),
    ("PVBI11", "VBI Prime"),
    ("KNSC11", "Kinea Sec"),
    ("MCCI11", "Mauá"),
    ("HFOF11", "Hedge FoF"),
    ("GGRC11", "GGR"),
    ("ALZR11", "Alianza"),
    ("LVBI11", "VBI Log"),
    ("KNHY11", "Kinea HY"),
)
FUNDAMENTUS_FII_URL = "https://www.fundamentus.com.br/fii_resultado.php"
INDEX_SYMBOL = "^BVSP"
YAHOO_CHART_URL = "https://query1.finance.yahoo.com/v8/finance/chart/{symbol}"
STOCKS_REFRESH_S = 300
FUNDAMENTUS_URL = "https://www.fundamentus.com.br/resultado.php"
FUNDAMENTALS_REFRESH_S = 3600   # P/VP moves with the daily close; hourly is plenty
MISSING = "-"                   # ticker absent from Fundamentus (e.g. BDRs like ROXO34)


def format_number(value: float, decimals: int | None = None) -> str:
    """pt-BR separators. Default precision: 0 above 1000, 2 above 1, else 4."""
    if decimals is None:
        decimals = 0 if value >= 1000 else 2 if value >= 1 else 4
    return f"{value:,.{decimals}f}".translate(str.maketrans(",.", ".,"))


def round_change(pct: float) -> float:
    """One decimal, with -0.0 folded to 0.0 so the device never shows "-0,0%"."""
    return round(float(pct), 1) + 0.0


def _dumps(obj: dict) -> str:
    # Same encoding the daemon writes with (ASCII-escaped), so the budget is exact.
    return json.dumps(obj, separators=(",", ":"))


def chunk_table(key: str, rows: list, header: dict | None = None) -> list[dict]:
    """Split rows into writes that each serialize to <= WRITE_BUDGET bytes."""
    payloads: list[dict] = []
    current = {key: [], "o": 0, "n": len(rows), **(header or {})}
    for row in rows:
        candidate = {**current, key: current[key] + [row]}
        if current[key] and len(_dumps(candidate).encode()) > WRITE_BUDGET:
            payloads.append(current)
            current = {key: [row], "o": current["o"] + len(current[key]), "n": len(rows)}
        else:
            current = candidate
    if current[key]:
        payloads.append(current)
    return payloads


def build_crypto_rows(data: dict) -> list:
    rows = []
    for coin_id, symbol in COINS:
        quote = data.get(coin_id) or {}
        brl, usd = quote.get("brl"), quote.get("usd")
        if not isinstance(brl, (int, float)) or not isinstance(usd, (int, float)):
            continue
        change = round_change(quote.get("brl_24h_change") or 0.0)
        rows.append([symbol, format_number(float(brl)), format_number(float(usd)), change])
    return rows


def parse_chart(meta: dict) -> tuple[float, float] | None:
    """(price, day change %) from a Yahoo chart `meta` block."""
    price = meta.get("regularMarketPrice")
    prev = meta.get("chartPreviousClose") or meta.get("previousClose")
    if not isinstance(price, (int, float)) or not isinstance(prev, (int, float)) or prev <= 0:
        return None
    return float(price), round_change((price - prev) / prev * 100)


def _ptbr_float(text: str) -> float | None:
    try:
        return float(text.replace(".", "").replace(",", ".").replace("%", "").strip())
    except ValueError:
        return None


def parse_fundamentus(page: str) -> dict[str, float]:
    """{ticker: P/VP} from Fundamentus' screener table."""
    headers = [html.unescape(re.sub(r"<[^>]+>", "", h)).strip()
               for h in re.findall(r"<th[^>]*>(.*?)</th>", page, re.S)]
    try:
        i_tick, i_pvp = headers.index("Papel"), headers.index("P/VP")
    except ValueError:
        return {}
    out = {}
    for tr in re.findall(r"<tr[^>]*>(.*?)</tr>", page, re.S):
        cells = [re.sub(r"<[^>]+>", "", c).strip() for c in re.findall(r"<td[^>]*>(.*?)</td>", tr, re.S)]
        if len(cells) != len(headers):
            continue
        pvp = _ptbr_float(cells[i_pvp])
        if pvp is not None:
            out[cells[i_tick]] = pvp
    return out


def format_pvp(pvp: float | None) -> str:
    """Fundamentus reports a missing ratio as 0."""
    return format_number(pvp, 2) if pvp else MISSING


class _Refreshing:
    """Caches the last good payload list; refetches at most every `refresh_s`."""

    refresh_s = 60
    headers: dict[str, str] = {}

    def __init__(self) -> None:
        self.payloads: list[dict] = []
        self.fetched_at = 0.0

    async def _fetch(self, http: httpx.AsyncClient) -> list[dict]:
        raise NotImplementedError

    async def get(self, now: float) -> list[dict]:
        if now - self.fetched_at < self.refresh_s:
            return self.payloads
        # Stamp before the request so a failing API isn't retried every cycle.
        self.fetched_at = now
        async with httpx.AsyncClient(timeout=10.0, headers=self.headers) as http:
            fresh = await self._fetch(http)
        if fresh:
            self.payloads = fresh
        return self.payloads


class CryptoQuotes(_Refreshing):
    refresh_s = CRYPTO_REFRESH_S

    async def _fetch(self, http: httpx.AsyncClient) -> list[dict]:
        resp = await http.get(COINGECKO_URL, params={
            "ids": ",".join(coin_id for coin_id, _ in COINS),
            "vs_currencies": "brl,usd",
            "include_24hr_change": "true",
        })
        resp.raise_for_status()
        return chunk_table("x", build_crypto_rows(resp.json()))


class StockQuotes(_Refreshing):
    """B3 tickers: Yahoo prices + Fundamentus P/VP. Stocks by default; FiiQuotes reuses it."""

    refresh_s = STOCKS_REFRESH_S
    headers = {"User-Agent": "Mozilla/5.0"}  # Yahoo and Fundamentus reject httpx's default UA
    key = "b"
    tickers = STOCKS
    fundamentus_url = FUNDAMENTUS_URL
    index_symbol: str | None = INDEX_SYMBOL

    def __init__(self) -> None:
        super().__init__()
        self.fundamentals: dict[str, float] = {}
        self.fundamentals_at = 0.0

    async def _fundamentals(self, http: httpx.AsyncClient, now: float) -> dict:
        if now - self.fundamentals_at < FUNDAMENTALS_REFRESH_S:
            return self.fundamentals
        self.fundamentals_at = now
        try:
            resp = await http.get(self.fundamentus_url)
            resp.raise_for_status()
            fresh = parse_fundamentus(resp.content.decode("latin-1"))
        except httpx.HTTPError:
            fresh = {}
        if fresh:
            self.fundamentals = fresh
        return self.fundamentals

    async def _chart(self, http: httpx.AsyncClient, symbol: str) -> tuple[float, float] | None:
        try:
            resp = await http.get(YAHOO_CHART_URL.format(symbol=symbol),
                                  params={"range": "1d", "interval": "1d"})
            resp.raise_for_status()
            return parse_chart(resp.json()["chart"]["result"][0]["meta"])
        except (httpx.HTTPError, ValueError, KeyError, IndexError, TypeError):
            return None

    async def _fetch(self, http: httpx.AsyncClient) -> list[dict]:
        symbols = [f"{ticker}.SA" for ticker, _ in self.tickers]
        if self.index_symbol:
            symbols.append(self.index_symbol)
        fundamentals, *results = await asyncio.gather(
            self._fundamentals(http, self.fetched_at), *(self._chart(http, s) for s in symbols))
        quotes = results[:len(self.tickers)]
        rows = [
            [ticker, name, format_number(q[0], 2), format_pvp(fundamentals.get(ticker)), q[1]]
            for (ticker, name), q in zip(self.tickers, quotes)
            if q is not None
        ]
        if not rows:
            return []
        index = results[-1] if self.index_symbol else None
        header = {"i": [format_number(index[0], 0), index[1]]} if index else None
        return chunk_table(self.key, rows, header)


class FiiQuotes(StockQuotes):
    key = "f"
    tickers = FIIS
    fundamentus_url = FUNDAMENTUS_FII_URL
    index_symbol = None
