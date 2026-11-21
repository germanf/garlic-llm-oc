import unittest

from tools.garlic_web_search import MAX_EXCERPT_CHARS, parse_results


class ParseResultsTests(unittest.TestCase):
    def test_extracts_only_http_sources_and_limits_count(self):
        payload = {"web": {"results": [
            {"title": "First", "url": "https://example.com/a", "description": "A useful excerpt"},
            {"title": "Bad scheme", "url": "javascript:alert(1)", "description": "ignore"},
            {"title": "Second", "url": "http://example.org", "description": "Second excerpt"},
        ]}}
        results = parse_results(payload, 1)
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0]["title"], "First")
        self.assertEqual(results[0]["url"], "https://example.com/a")

    def test_bounds_untrusted_fields(self):
        payload = {"web": {"results": [{
            "title": "t" * 500,
            "url": "https://example.com/" + "x" * 3000,
            "description": "s" * (MAX_EXCERPT_CHARS + 500),
        }]}}
        result = parse_results(payload, 5)[0]
        self.assertEqual(len(result["title"]), 300)
        self.assertEqual(len(result["url"]), 2048)
        self.assertEqual(len(result["snippet"]), MAX_EXCERPT_CHARS)

    def test_handles_missing_or_malformed_results(self):
        self.assertEqual(parse_results({}, 5), [])
        self.assertEqual(parse_results({"web": {"results": [None, {"url": "https://example.com"}]}}, 5), [])


if __name__ == "__main__":
    unittest.main()
