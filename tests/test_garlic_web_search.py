import unittest

from tools.garlic_web_search import MAX_EXCERPT_CHARS, parse_results


class DuckDuckGoParserTests(unittest.TestCase):
    def test_extracts_result_title_link_and_snippet(self):
        document = '''<div class="results">
          <div class="result results_links">
            <a class="result__a" href="https://example.com/a">Example <b>title</b></a>
            <a class="result__snippet">A useful <b>excerpt</b> for the result.</a>
          </div>
          <div class="result results_links">
            <a class="result__a" href="https://example.org/">Another title</a>
            <td class="result__snippet">Second excerpt</td>
          </div>
        </div>'''
        results = parse_results(document, 1)
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0]["title"], "Example title")
        self.assertEqual(results[0]["url"], "https://example.com/a")
        self.assertEqual(results[0]["snippet"], "A useful excerpt for the result.")

    def test_unwraps_redirect_and_rejects_non_web_urls(self):
        document = '''<div class="result"><a class="result__a" href="//duckduckgo.com/l/?uddg=https%3A%2F%2Fexample.com%2Fx">Good</a></div>
        <div class="result"><a class="result__a" href="javascript%3Aalert(1)">Bad</a></div>'''
        results = parse_results(document)
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0]["url"], "https://example.com/x")

    def test_limits_excerpt_and_handles_empty_or_malformed_markup(self):
        text = "x" * (MAX_EXCERPT_CHARS + 500)
        document = f'<div class="result"><a class="result__a" href="https://example.com">Title</a><a class="result__snippet">{text}</a></div>'
        self.assertEqual(len(parse_results(document)[0]["snippet"]), MAX_EXCERPT_CHARS)
        self.assertEqual(parse_results("<html><body>No results</body></html>"), [])


if __name__ == "__main__":
    unittest.main()
