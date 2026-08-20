import math

import pytest

from sample.protocol import ProtocolError, parse_assignments, summarize_response


def test_parses_supported_scalar_and_list_values():
    response = 'name="sensor\\nA"\nenabled=True\ncount=-12\nratio=1.25e-3\nvalues=[1, 2.5, "x", False,]\nempty=[]'

    assert parse_assignments(response) == {
        "name": "sensor\nA",
        "enabled": True,
        "count": -12,
        "ratio": 1.25e-3,
        "values": [1, 2.5, "x", False],
        "empty": [],
    }


@pytest.mark.parametrize("token", [
    "nan", "+nan", "-nan", "1.#IND", "-1.#IND", "1.#QNAN", "-1.#QNAN", "1.#SNAN", "-1.#SNAN",
])
def test_parses_all_nan_tokens_case_insensitively(token):
    assert math.isnan(parse_assignments("value=" + token.swapcase())["value"])


@pytest.mark.parametrize("token", ["inf", "+inf", "infinity", "+infinity", "1.#INF", "+1.#INF"])
def test_parses_all_positive_infinity_tokens_case_insensitively(token):
    assert parse_assignments("value=" + token.swapcase())["value"] == float("inf")


@pytest.mark.parametrize("token", ["-inf", "-infinity", "-1.#INF"])
def test_parses_all_negative_infinity_tokens_case_insensitively(token):
    assert parse_assignments("value=" + token.swapcase())["value"] == float("-inf")


def test_does_not_replace_special_tokens_inside_strings():
    assert parse_assignments('value="nan inf 1.#IND"') == {"value": "nan inf 1.#IND"}


def test_unknown_fields_are_parsed_safely():
    assert parse_assignments("future_field=[1, 2]") == {"future_field": [1, 2]}


@pytest.mark.parametrize("response", [
    "x=func()", "x=obj.attr", "x=__import__(\"os\")", "x=1 + 2", "x=[v for v in []]", "x={\"a\": 1}",
    "x=(1, 2)", "x=[[1]]", "x=1; y=2", "x=1 trailing", "bad-name=1", "x=1\nx=2", 'x="unterminated',
    "x=[1, 2", 'x="bad\\q"', "x=None", "x=true", "x=0x10", "x=", "=1",
])
def test_rejects_unsafe_or_malformed_input(response):
    with pytest.raises(ProtocolError):
        parse_assignments(response)


def test_rejects_non_ascii_field_names():
    with pytest.raises(ProtocolError):
        parse_assignments("café=1")


def test_rejects_python_keywords_as_field_names():
    with pytest.raises(ProtocolError):
        parse_assignments("class=1")


def test_known_types_accepts_type_or_tuple_and_checks_exact_type():
    assert parse_assignments("count=1\nvalue=2", {"count": int, "value": (int, float)}) == {"count": 1, "value": 2}
    with pytest.raises(ProtocolError) as caught:
        parse_assignments("count=True", {"count": int})
    assert caught.value.field_name == "count"


@pytest.mark.parametrize("token, expected", [("True", True), ("False", False)])
def test_known_bool_field_accepts_only_bool_literals(token, expected):
    assert parse_assignments("v_inf_valid = " + token, {"v_inf_valid": bool}) == {"v_inf_valid": expected}


def test_known_bool_field_rejects_integer_value():
    with pytest.raises(ProtocolError):
        parse_assignments("v_inf_valid=1", {"v_inf_valid": bool})


@pytest.mark.parametrize("known_types", [None, {"_v_inf_reported": bool}])
def test_rejects_private_response_fields_even_when_known(known_types):
    with pytest.raises(ProtocolError):
        parse_assignments("_v_inf_reported=True", known_types)


def test_protocol_error_has_bounded_diagnostics():
    response = "good=1\nfield=" + ("x" * 2000) + "\nlast=2"
    with pytest.raises(ProtocolError) as caught:
        parse_assignments(response)
    error = caught.value
    assert error.line_number == 2
    assert error.field_name == "field"
    assert error.response_length == len(response)
    assert len(error.response_summary) <= 512
    assert "good=1\\n" in error.response_summary
    assert "last=2" in error.response_summary
    assert response not in str(error)


def test_summarize_response_escapes_controls_and_honors_small_limits():
    summary = summarize_response("a\\b\r\nc\t\x01\x85" + ("z" * 100), limit=28)
    assert len(summary) <= 28
    assert "\\\\" in summary
    assert "\\r" in summary
    assert "\\n" in summary
    assert summarize_response("\x85") == "\\x85"
    assert summarize_response("anything", limit=0) == ""


@pytest.mark.parametrize("limit", range(1, 9))
def test_summarize_response_honors_every_small_positive_limit(limit):
    summary = summarize_response("abcdefghijklmnopqrstuvwxyz", limit=limit)
    assert len(summary) <= limit
    if limit >= 5:
        assert summary.startswith("a")
        assert summary.endswith("z")


def test_invalid_long_field_name_is_bounded_in_diagnostics():
    response = ("x" * 2000) + "-=1"
    with pytest.raises(ProtocolError) as caught:
        parse_assignments(response)
    error = caught.value
    assert error.field_name is not None
    assert len(error.field_name) <= 120
    assert len(str(error)) <= 800
    assert len(error.response_summary) <= 512
    assert error.response_length == len(response)


def test_summarize_large_response_does_not_iterate_over_the_whole_input():
    class IterationGuard(str):
        def __iter__(self):
            for index, character in enumerate(super(IterationGuard, self).__iter__()):
                if index >= 1024:
                    raise AssertionError("summarize_response traversed too much input")
                yield character

    response = IterationGuard("start\n" + ("x" * (4 * 1024 * 1024)) + "\rend")
    summary = summarize_response(response, limit=64)
    assert len(summary) <= 64
    assert summary.startswith("start\\n")
    assert summary.endswith("\\rend")
