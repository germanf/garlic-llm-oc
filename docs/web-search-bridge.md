# DuckDuckGo web-search bridge for Garlic LLM

This optional computer-side bridge uses DuckDuckGo's server-rendered HTML results and sends a small set of text excerpts to Garlic's existing OpenAI-compatible API on the PS4's local network. The language model stays on the console. DuckDuckGo search requires no API key or account for this basic use, but its HTML endpoint may change or rate-limit automated requests.

The bridge is a command-line client, not an internet-facing server. It does not expose a new listening port, fetch arbitrary result pages, execute JavaScript, or make the PS4 browse the web. It uses Python's standard library only.

## Requirements

- Garlic LLM with its OpenAI-compatible API (available in releases that include the API, version 1.20 or newer).
- PS4 and computer on the same trusted local network.
- Python 3.
- Internet access on the computer running this bridge.

## Start the PS4 API

Open Garlic on the PS4 and press **L3** on the chat screen. Note the address shown, such as `192.168.1.5:8080`. From the computer, check `http://192.168.1.5:8080/health`. If `/data/garlic_api_key.txt` is configured on the console, set the same value in `GARLIC_API_KEY` on the computer.

## Search and ask Garlic

```sh
export GARLIC_API_URL='http://192.168.1.5:8080'
# Only when Garlic's local API key is configured:
export GARLIC_API_KEY='your-Garlic-API-key'
python3 tools/garlic_web_search.py "¿Qué novedades hay sobre ...?"
```

Windows PowerShell:

```powershell
$env:GARLIC_API_URL = 'http://192.168.1.5:8080'
# Only when configured on the PS4:
$env:GARLIC_API_KEY = 'your-Garlic-API-key'
python tools/garlic_web_search.py "¿Qué novedades hay sobre ...?"
```

Options: `--ps4 http://<PS4-IP>:8080`, `--count 1..10` (default 5), and `--timeout 1..60` seconds. There is no Brave credential or search API key to configure. The Garlic API handles one generation request at a time.

## Safety and limitations

- The question is sent to DuckDuckGo. Result titles, URLs, and bounded excerpts are sent to Garlic over the local network. Do not search for private information.
- DuckDuckGo HTML is parsed as text only; scripts are never executed. The client follows no links from search results.
- The bridge accepts only HTTP(S) result URLs, caps the downloaded response at 2 MiB, limits result count and excerpt size, and applies request timeouts.
- Search excerpts are untrusted; the prompt tells the model not to follow instructions in them. This is a mitigation, not a guarantee against prompt injection.
- DuckDuckGo may change the HTML, present a challenge, or throttle automated requests. In that case the bridge reports that no usable results were returned; avoid aggressive retries and follow the service's terms.
- Garlic's API uses HTTP on the LAN. Use a trusted network, configure its API-key option if appropriate, and do not forward port 8080 from the router to the public internet.

## Offline parser tests

Tests use fixed HTML fixtures embedded in the test file and make no external requests:

```sh
python -m unittest discover -s tests -p 'test_garlic_web_search.py'
```
