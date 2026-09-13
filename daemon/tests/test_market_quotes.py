import json

from daemon.market_quotes import (
    WRITE_BUDGET,
    build_crypto_rows,
    chunk_table,
    format_number,
    format_pvp,
    parse_chart,
    parse_fundamentus,
)


def test_format_number_ptbr():
    assert format_number(395187) == "395.187"
    assert format_number(274.2201) == "274,22"
    assert format_number(0.433806) == "0,4338"
    assert format_number(49.0, 2) == "49,00"
    assert format_number(187206.89, 0) == "187.207"


def test_build_crypto_rows_needs_both_currencies():
    data = {
        "bitcoin": {"brl": 395187, "usd": 72340.5, "brl_24h_change": -0.069},
        "ethereum": {"brl": 12904.04},
    }
    assert build_crypto_rows(data) == [["BTC", "395.187", "72.340", -0.1]]


def test_parse_chart_day_change():
    assert parse_chart({"regularMarketPrice": 49.0, "chartPreviousClose": 49.12}) == (49.0, -0.2)
    assert parse_chart({"regularMarketPrice": 49.0}) is None
    assert str(parse_chart({"regularMarketPrice": 78.199, "chartPreviousClose": 78.2})[1]) == "0.0"


def test_chunk_table_respects_budget_and_offsets():
    rows = [[f"T{i}", "Nome longo", "1.234,56", -1.5] for i in range(8)]
    payloads = chunk_table("b", rows, {"i": ["187.207", -0.6]})
    assert len(payloads) > 1
    assert payloads[0]["i"] == ["187.207", -0.6]
    assert all("i" not in p for p in payloads[1:])
    offset = 0
    for p in payloads:
        assert len(json.dumps(p, separators=(",", ":")).encode()) <= WRITE_BUDGET
        assert p["o"] == offset and p["n"] == 8
        offset += len(p["b"])
    assert offset == 8


FUNDAMENTUS_PAGE = """<table><thead><tr><th>Papel</th><th>Cota&ccedil;&atilde;o</th><th>P/L</th>
<th>P/VP</th><th>Div.Yield</th></tr></thead><tbody>
<tr class="par"><td><span><a href="x">BBAS3</a></span></td><td>22,49</td><td>8,90</td><td>0,71</td><td>3,06%</td></tr>
<tr class=""><td><a>BRAV3</a></td><td>18,29</td><td>1.156,03</td><td>0,69</td><td>0,68%</td></tr>
</tbody></table>"""


def test_parse_fundamentus_rows():
    assert parse_fundamentus(FUNDAMENTUS_PAGE) == {"BBAS3": 0.71, "BRAV3": 0.69}
    assert parse_fundamentus("<html>mudou</html>") == {}


def test_format_pvp():
    assert format_pvp(0.714) == "0,71"
    assert format_pvp(0.0) == "-"
    assert format_pvp(None) == "-"
