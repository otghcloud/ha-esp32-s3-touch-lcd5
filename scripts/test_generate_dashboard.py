import copy
import unittest

import yaml

from generate_dashboard import CONFIG_PATH, make_dashboard, validate_config


class PageIndicatorTests(unittest.TestCase):
    def setUp(self):
        with CONFIG_PATH.open(encoding="utf-8") as config_file:
            self.pages = validate_config(yaml.safe_load(config_file))

    def test_single_page_has_no_indicator(self):
        page = make_dashboard(self.pages[:1])["lvgl"]["pages"][0]
        self.assertFalse(any("obj" in widget for widget in page["widgets"]))

    def test_each_page_highlights_only_its_own_dot(self):
        for count in (2, 3):
            with self.subTest(page_count=count):
                pages = [copy.deepcopy(self.pages[0]) for _ in range(count)]
                for index in range(count):
                    pages[index]["id"] = f"page_{index}"
                result = make_dashboard(pages)["lvgl"]["pages"]
                for page_index, page in enumerate(result):
                    indicator = next(
                        widget["obj"] for widget in page["widgets"] if "obj" in widget
                    )
                    dots = indicator["widgets"]
                    self.assertEqual(len(dots), count)
                    self.assertEqual(indicator["align"], "BOTTOM_MID")
                    self.assertEqual(
                        [dot["obj"]["bg_opa"] for dot in dots],
                        [
                            "COVER" if index == page_index else "TRANSP"
                            for index in range(count)
                        ],
                    )
                    self.assertLessEqual(
                        dots[-1]["obj"]["x"] + dots[-1]["obj"]["width"],
                        indicator["width"],
                    )
                    self.assertFalse(indicator["clickable"])


if __name__ == "__main__":
    unittest.main()
