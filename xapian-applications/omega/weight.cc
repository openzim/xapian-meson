/** @file
 * @brief Set the weighting scheme for Omega
 */
/* Copyright (C) 2009,2013,2016,2024 Olly Betts
 * Copyright (C) 2013 Aarsh Shah
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <config.h>

#include "weight.h"

#include "stringutils.h"

#include <cerrno>
#include <cstdlib>
#include "common/noreturn.h"

using namespace std;

XAPIAN_NORETURN(static void
parameter_error(const char * param, const string & scheme));

static void
parameter_error(const char * msg, const string & scheme)
{
    string m(msg);
    m += ": '";
    m += scheme;
    m += "'";
    throw m;
}

static bool
double_param(const char ** p, double * ptr_val)
{
    char *end;
    errno = 0;
    double v = strtod(*p, &end);
    if (*p == end || errno) return false;
    *p = end;
    *ptr_val = v;
    return true;
}

static bool
type_smoothing_param(const char ** p, Xapian::Weight::type_smoothing * ptr_val)
{
    char *end;
    errno = 0;
    int v = strtol(*p, &end, 10);
    if (*p == end || errno || v < 1 || v > 4)
	return false;
    *p = end;
    static const Xapian::Weight::type_smoothing smooth_tab[4] = {
	Xapian::Weight::TWO_STAGE_SMOOTHING,
	Xapian::Weight::DIRICHLET_SMOOTHING,
	Xapian::Weight::ABSOLUTE_DISCOUNT_SMOOTHING,
	Xapian::Weight::JELINEK_MERCER_SMOOTHING
    };
    *ptr_val = smooth_tab[v - 1];
    return true;
}

void
set_weighting_scheme(Xapian::Enquire & enq, const string & scheme,
		     bool force_boolean)
{
    if (scheme.empty()) return;

    if (force_boolean || scheme == "bool") {
	enq.set_weighting_scheme(Xapian::BoolWeight());
	return;
    }

    if (startswith(scheme, "bm25")) {
	const char* p = scheme.c_str() + 4;
	if (*p == '+') {
	    // bm25+:
	    ++p;
	    if (*p == '\0') {
		enq.set_weighting_scheme(Xapian::BM25PlusWeight());
		return;
	    }
	    if (!C_isspace(*p))
		goto unknown;

	    double k1 = 1;
	    double k2 = 0;
	    double k3 = 1;
	    double b = 0.5;
	    double min_normlen = 0.5;
	    double delta = 1.0;
	    if (!double_param(&p, &k1))
		parameter_error("Parameter 1 (k1) is invalid", scheme);
	    if (*p && !double_param(&p, &k2))
		parameter_error("Parameter 2 (k2) is invalid", scheme);
	    if (*p && !double_param(&p, &k3))
		parameter_error("Parameter 3 (k3) is invalid", scheme);
	    if (*p && !double_param(&p, &b))
		parameter_error("Parameter 4 (b) is invalid", scheme);
	    if (*p && !double_param(&p, &min_normlen))
		parameter_error("Parameter 5 (min_normlen) is invalid",
				scheme);
	    if (*p && !double_param(&p, &delta))
		parameter_error("Parameter 6 (delta) is invalid", scheme);
	    if (*p)
		parameter_error("Extra data after parameter 6", scheme);
	    Xapian::BM25PlusWeight wt(k1, k2, k3, b, min_normlen, delta);
	    enq.set_weighting_scheme(wt);
	    return;
	}

	// bm25:
	if (*p == '\0') {
	    enq.set_weighting_scheme(Xapian::BM25Weight());
	    return;
	}
	if (!C_isspace(*p))
	    goto unknown;

	double k1 = 1;
	double k2 = 0;
	double k3 = 1;
	double b = 0.5;
	double min_normlen = 0.5;
	if (!double_param(&p, &k1))
	    parameter_error("Parameter 1 (k1) is invalid", scheme);
	if (*p && !double_param(&p, &k2))
	    parameter_error("Parameter 2 (k2) is invalid", scheme);
	if (*p && !double_param(&p, &k3))
	    parameter_error("Parameter 3 (k3) is invalid", scheme);
	if (*p && !double_param(&p, &b))
	    parameter_error("Parameter 4 (b) is invalid", scheme);
	if (*p && !double_param(&p, &min_normlen))
	    parameter_error("Parameter 5 (min_normlen) is invalid", scheme);
	if (*p)
	    parameter_error("Extra data after parameter 5", scheme);
	Xapian::BM25Weight wt(k1, k2, k3, b, min_normlen);
	enq.set_weighting_scheme(wt);
	return;
    }

    if (startswith(scheme, "trad")) {
	const char* p = scheme.c_str() + 4;
	if (*p == '\0') {
	    enq.set_weighting_scheme(Xapian::TradWeight());
	    return;
	}
	if (!C_isspace(*p))
	    goto unknown;

	double k;
	if (!double_param(&p, &k))
	    parameter_error("Parameter is invalid", scheme);
	if (*p)
	    parameter_error("Extra data after parameter", scheme);
	enq.set_weighting_scheme(Xapian::TradWeight(k));
	return;
    }

    if (startswith(scheme, "tfidf")) {
	const char* p = scheme.c_str() + 5;
	if (*p == '\0') {
	    enq.set_weighting_scheme(Xapian::TfIdfWeight());
	    return;
	}
	if (!C_isspace(*p))
	    goto unknown;

	enq.set_weighting_scheme(Xapian::TfIdfWeight(p + 1));
	return;
    }

    if (startswith(scheme, "inl2")) {
	const char* p = scheme.c_str() + 4;
	if (*p == '\0') {
	    enq.set_weighting_scheme(Xapian::InL2Weight());
	    return;
	}
	if (!C_isspace(*p))
	    goto unknown;

	double c;
	if (!double_param(&p, &c))
	    parameter_error("Parameter is invalid", scheme);
	if (*p)
	    parameter_error("Extra data after parameter", scheme);
	enq.set_weighting_scheme(Xapian::InL2Weight(c));
	return;
    }

    if (startswith(scheme, "ifb2")) {
	const char* p = scheme.c_str() + 4;
	if (*p == '\0') {
	    enq.set_weighting_scheme(Xapian::IfB2Weight());
	    return;
	}
	if (!C_isspace(*p))
	    goto unknown;

	double c;
	if (!double_param(&p, &c))
	    parameter_error("Parameter is invalid", scheme);
	if (*p)
	    parameter_error("Extra data after parameter", scheme);
	enq.set_weighting_scheme(Xapian::IfB2Weight(c));
	return;
    }

    if (startswith(scheme, "ineb2")) {
	const char* p = scheme.c_str() + 5;
	if (*p == '\0') {
	    enq.set_weighting_scheme(Xapian::IneB2Weight());
	    return;
	}
	if (!C_isspace(*p))
	    goto unknown;

	double c;
	if (!double_param(&p, &c))
	    parameter_error("Parameter is invalid", scheme);
	if (*p)
	    parameter_error("Extra data after parameter", scheme);
	enq.set_weighting_scheme(Xapian::IneB2Weight(c));
	return;
    }

    if (startswith(scheme, "bb2")) {
	const char* p = scheme.c_str() + 3;
	if (*p == '\0') {
	    enq.set_weighting_scheme(Xapian::BB2Weight());
	    return;
	}
	if (!C_isspace(*p))
	    goto unknown;

	double c;
	if (!double_param(&p, &c))
	    parameter_error("Parameter is invalid", scheme);
	if (*p)
	    parameter_error("Extra data after parameter", scheme);
	enq.set_weighting_scheme(Xapian::BB2Weight(c));
	return;
    }

    if (startswith(scheme, "dlh")) {
	const char* p = scheme.c_str() + 3;
	if (*p == '\0') {
	    enq.set_weighting_scheme(Xapian::DLHWeight());
	    return;
	}
	if (!C_isspace(*p))
	    goto unknown;

	throw "No parameters are required for DLH";
    }

    if (startswith(scheme, "pl2")) {
	const char* p = scheme.c_str() + 3;
	if (*p == '+') {
	    // pl2+:
	    ++p;
	    if (*p == '\0') {
		enq.set_weighting_scheme(Xapian::PL2PlusWeight());
		return;
	    }
	    if (!C_isspace(*p))
		goto unknown;

	    double c = 1.0;
	    double delta = 0.8;
	    if (!double_param(&p, &c))
		parameter_error("Parameter 1 (c) is invalid", scheme);
	    if (*p && !double_param(&p, &delta))
		parameter_error("Parameter 2 (delta) is invalid", scheme);
	    if (*p)
		parameter_error("Extra data after parameter 2", scheme);
	    enq.set_weighting_scheme(Xapian::PL2PlusWeight(c, delta));
	    return;
	}

	// pl2:
	if (*p == '\0') {
	    enq.set_weighting_scheme(Xapian::PL2Weight());
	    return;
	}
	if (!C_isspace(*p))
	    goto unknown;

	double c;
	if (!double_param(&p, &c))
	    parameter_error("Parameter is invalid", scheme);
	if (*p)
	    parameter_error("Extra data after parameter", scheme);
	enq.set_weighting_scheme(Xapian::PL2Weight(c));
	return;
    }

    if (startswith(scheme, "dph")) {
	const char* p = scheme.c_str() + 3;
	if (*p == '\0') {
	    enq.set_weighting_scheme(Xapian::DPHWeight());
	    return;
	}
	if (!C_isspace(*p))
	    goto unknown;

	throw "No parameters are required for DPH";
    }

    if (startswith(scheme, "lm")) {
	const char* p = scheme.c_str() + 2;
	if (*p == '\0') {
	    enq.set_weighting_scheme(Xapian::LMWeight());
	    return;
	}
	if (!C_isspace(*p))
	    goto unknown;

	double param_log = 0;
	Xapian::Weight::type_smoothing type =
	    Xapian::Weight::TWO_STAGE_SMOOTHING;
	double smoothing1 = 0.7;
	double smoothing2 = 2000;
	if (!double_param(&p, &param_log))
	    parameter_error("Parameter 1 (log) is invalid", scheme);
	if (*p && !type_smoothing_param(&p, &type))
	    parameter_error("Parameter 2 (smoothing_type) is invalid", scheme);
	if (*p && !double_param(&p, &smoothing1))
	    parameter_error("Parameter 3 (smoothing1) is invalid", scheme);
	if (*p && !double_param(&p, &smoothing2))
	    parameter_error("Parameter 4 (smoothing2) is invalid", scheme);
	if (*p)
	    parameter_error("Extra data after parameter 4", scheme);
	Xapian::LMWeight wt(param_log, type, smoothing1, smoothing2);
	enq.set_weighting_scheme(wt);
	return;
    }

    if (scheme == "coord") {
	enq.set_weighting_scheme(Xapian::CoordWeight());
	return;
    }

unknown:
    throw "Unknown $opt{weighting} setting: " + scheme;
}
