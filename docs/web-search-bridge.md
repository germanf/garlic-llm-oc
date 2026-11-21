# Web search bridge for Garlic LLM

Garlic's model runs locally on the PS4. This optional computer-side bridge uses Brave Search for current web results, then sends the question and bounded text excerpts to Garlic's existing OpenAI-compatible **LAN API** for a locally generated, source-linked answer. The Brave credential stays on the computer running the bridge; it is not embedded in the PKG, committed to Git, or sent to the console.

This is deliberately a client-side bridge rather than an internet-facing server. It does not expose a new listening port, crawl arbitrary URLs, execute page code, or expose the PS4 API to the public internet. Search results are untrusted input and are labeled as such in the model prompt.

## Requirements

- Garlic LLM with its OpenAI-compatible API (available in releases that include the API, version 1.20 or newer).
- PS4 and computer on the same trusted local network.
- A Brave Search API subscription/key.
- Python 3; the bridge uses only the standard library.

## Start the PS4 API

Open Garlic on the PS4 and press **L3** on the chat screen. Note the address shown, such as `192.168.1.5:8080`. Confirm the computer can reach `http://192.168.1.5:8080/health`. If you configured `/data/garlic_api_key.txt`, also set `GARLIC_API_KEY` on the computer to the matching value.

## Run a search

Set credentials in your computer's environment (do not paste them into source files or commit them):

```sh
export BRAVE_SEARCH_API_KEY='your-Brave-key'
export GARLIC_API_URL='http://192.168.1.5:8080'
# Only if Garlic's local API key is configured:
export GARLIC_API_KEY='your-Garlic-API-key'
python3 tools/garlic_web_search.py "¿Qué novedades hay sobre ...?"
```

Windows PowerShell example:

```powershell
$env:BRAVE_SEARCH_API_KEY = 'your-Brave-key'
$env:GARLIC_API_URL = 'http://192.168.1.5:8080'
python tools/garlic_web_search.py "¿Qué novedades hay sobre ...?"
```

Options: `--ps4 http://<PS4-IP>:8080`, `--count 1..10` (default 5), and `--timeout 1..60` seconds. The PS4 API has one model request at a time; the bridge waits for its response.

## Data and security notes

- Each search sends the question to Brave. The returned titles, URLs, and excerpts are sent over the local network to the PS4's Garlic API. Do not include private information in a search query.
- Keep both API keys in environment variables or a local, untracked secrets manager. Never commit them. The Brave key is sent to Brave over HTTPS and is not forwarded to Garlic.
- The PS4 API uses HTTP on the LAN. Use it only on a trusted network; retain Garlic's API-key option and do not forward port 8080 from your router to the internet.
- The bridge contacts only Brave's fixed search endpoint; it does not fetch arbitrary result URLs. It caps the response at 2 MiB, limits result count and excerpt length, and applies request timeouts.
- Brave Search may require a paid or otherwise eligible account; check the current terms and quotas for your account.

## Tests

```sh
python -m unittest discover -s tests -p 'test_garlic_web_search.py'
```
