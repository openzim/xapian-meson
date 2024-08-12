/*
** 2000-05-29
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** Driver template for the LEMON parser generator.
**
** Synced with upstream:
** https://www.sqlite.org/src/artifact/468a155e8729cfbccfe1d85bf60d064f1dab76167a51149ec5c7928a2de63953
**
** The "lemon" program processes an LALR(1) input grammar file, then uses
** this template to construct a parser.  The "lemon" program inserts text
** at each "%%" line.  Also, any "P-a-r-s-e" identifier prefix (without the
** interstitial "-" characters) contained in this template is changed into
** the value of the %name directive from the grammar.  Otherwise, the content
** of this template is copied straight through into the generate parser
** source file.
**
** The following is the concatenation of all %include directives from the
** input grammar file:
*/
/************ Begin %include sections from the grammar ************************/
#line 1 "queryparser/queryparser.lemony"

/** @file
 * @brief build a Xapian::Query object from a user query string
 */
/* Copyright (C) 2004-2023 Olly Betts
 * Copyright (C) 2007,2008,2009 Lemur Consulting Ltd
 * Copyright (C) 2010 Adam Sjøgren
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */

#include <config.h>

#include "queryparser_internal.h"

#include "api/queryinternal.h"
#include "omassert.h"
#include "str.h"
#include "stringutils.h"
#include "xapian/error.h"
#include "xapian/unicode.h"

// Include the list of token values lemon generates.
#include "queryparser_token.h"

#include "word-breaker.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <list>
#include <string>
#include <vector>

// We create the yyParser on the stack.
#define Parse_ENGINEALWAYSONSTACK

using namespace std;

using namespace Xapian;

inline bool
U_isupper(unsigned ch) {
    return ch < 128 && C_isupper(static_cast<unsigned char>(ch));
}

inline bool
U_isdigit(unsigned ch) {
    return ch < 128 && C_isdigit(static_cast<unsigned char>(ch));
}

inline bool
U_isalpha(unsigned ch) {
    return ch < 128 && C_isalpha(static_cast<unsigned char>(ch));
}

using Xapian::Unicode::is_whitespace;

inline bool
is_not_whitespace(unsigned ch) {
    return !is_whitespace(ch);
}

using Xapian::Unicode::is_wordchar;

inline bool
is_not_wordchar(unsigned ch) {
    return !is_wordchar(ch);
}

inline bool
is_digit(unsigned ch) {
    return (Unicode::get_category(ch) == Unicode::DECIMAL_DIGIT_NUMBER);
}

// FIXME: we used to keep trailing "-" (e.g. Cl-) but it's of dubious utility
// and there's the risk of hyphens getting stuck onto the end of terms...
inline bool
is_suffix(unsigned ch) {
    return ch == '+' || ch == '#';
}

inline bool
is_double_quote(unsigned ch) {
    // We simply treat all double quotes as equivalent, which is a bit crude,
    // but it isn't clear that it would actually better to require them to
    // match up exactly.
    //
    // 0x201c is Unicode opening double quote.
    // 0x201d is Unicode closing double quote.
    return ch == '"' || ch == 0x201c || ch == 0x201d;
}

inline bool
prefix_needs_colon(const string & prefix, unsigned ch)
{
    if (!U_isupper(ch) && ch != ':') return false;
    string::size_type len = prefix.length();
    return (len > 1 && prefix[len - 1] != ':');
}

using Unicode::is_currency;

inline bool
is_positional(Xapian::Query::op op)
{
    return (op == Xapian::Query::OP_PHRASE || op == Xapian::Query::OP_NEAR);
}

class Terms;

/** Class used to pass information about a token from lexer to parser.
 *
 *  Generally an instance of this class carries term information, but it can be
 *  used for a range query, and with some operators (e.g. the distance in
 *  NEAR/3 or ADJ/3, etc).
 */
class Term {
    State * state;

  public:
    string name;
    const FieldInfo * field_info;
    string unstemmed;
    QueryParser::stem_strategy stem;
    termpos pos;
    Query query;

    Term(const string &name_, termpos pos_)
	: name(name_), stem(QueryParser::STEM_NONE), pos(pos_) { }
    explicit Term(const string &name_)
	: name(name_), stem(QueryParser::STEM_NONE), pos(0) { }
    Term(const string &name_, const FieldInfo * field_info_)
	: name(name_), field_info(field_info_),
	  stem(QueryParser::STEM_NONE), pos(0) { }
    explicit Term(termpos pos_) : stem(QueryParser::STEM_NONE), pos(pos_) { }
    Term(State * state_, const string &name_, const FieldInfo * field_info_,
	 const string &unstemmed_,
	 QueryParser::stem_strategy stem_ = QueryParser::STEM_NONE,
	 termpos pos_ = 0)
	: state(state_), name(name_), field_info(field_info_),
	  unstemmed(unstemmed_), stem(stem_), pos(pos_) { }
    // For RANGE tokens.
    Term(const Xapian::Query & q, const string & grouping)
	: name(grouping), query(q) { }

    string make_term(const string & prefix) const;

    void need_positions() {
	if (stem == QueryParser::STEM_SOME) stem = QueryParser::STEM_NONE;
    }

    termpos get_termpos() const { return pos; }

    string get_grouping() const {
	return field_info->grouping;
    }

    Query * as_wildcarded_query(State * state) const;

    /** Build a query for a term at the very end of the query string when
     *  FLAG_PARTIAL is in use.
     *
     *  This query should match documents containing any terms which start with
     *  the characters specified, but should give a higher score to exact
     *  matches (since the user might have finished typing - we simply don't
     *  know).
     */
    Query * as_partial_query(State * state_) const;

    /** Build a query for a string of words without explicit word breaks. */
    Query* as_unbroken_query() const;

    /** Handle text without explicit word breaks in a positional context. */
    void as_positional_unbroken(Terms* terms) const;

    /// Range query.
    Query as_range_query() const;

    Query get_query() const;

    Query get_query_with_synonyms() const;

    Query get_query_with_auto_synonyms() const;
};

/// Parser State shared between the lexer and the parser.
class State {
    QueryParser::Internal * qpi;

  public:
    Query query;
    const char* error = NULL;
    unsigned flags;
    Query::op effective_default_op;

    State(QueryParser::Internal * qpi_, unsigned flags_)
	: qpi(qpi_), flags(flags_), effective_default_op(qpi_->default_op)
    {
	if ((flags & QueryParser::FLAG_NO_POSITIONS)) {
	    if (is_positional(effective_default_op)) {
		effective_default_op = Query::OP_AND;
		}
	}
    }

    string stem_term(const string &term) {
	return qpi->stemmer(term);
    }

    void add_to_stoplist(const Term * term) {
	qpi->stoplist.push_back(term->name);
    }

    void add_to_unstem(const string & term, const string & unstemmed) {
	qpi->unstem.insert(make_pair(term, unstemmed));
    }

    Term * range(const string &a, const string &b) {
	for (auto i : qpi->rangeprocs) {
	    Xapian::Query range_query = (i.proc)->check_range(a, b);
	    Xapian::Query::op op = range_query.get_type();
	    switch (op) {
		case Xapian::Query::OP_INVALID:
		    break;
		case Xapian::Query::OP_VALUE_RANGE:
		case Xapian::Query::OP_VALUE_GE:
		case Xapian::Query::OP_VALUE_LE:
		    if (i.default_grouping) {
			Xapian::Internal::QueryValueBase * base =
			    static_cast<Xapian::Internal::QueryValueBase*>(
				range_query.internal.get());
			Xapian::valueno slot = base->get_slot();
			return new Term(range_query, str(slot));
		    }
		    // FALLTHRU
		case Xapian::Query::LEAF_TERM:
		    return new Term(range_query, i.grouping);
		default:
		    return new Term(range_query, string());
	    }
	}
	return NULL;
    }

    Query::op default_op() const {
	return effective_default_op;
    }

    bool is_stopword(const Term *term) const {
	return qpi->stopper.get() && (*qpi->stopper)(term->name);
    }

    Database get_database() const {
	return qpi->db;
    }

    const Stopper * get_stopper() const {
	return qpi->stopper.get();
    }

    size_t stoplist_size() const {
	return qpi->stoplist.size();
    }

    void stoplist_resize(size_t s) {
	qpi->stoplist.resize(s);
    }

    Xapian::termcount get_max_wildcard_expansion() const {
	return qpi->max_wildcard_expansion;
    }

    int get_max_wildcard_type() const {
	return qpi->max_wildcard_type;
    }

    Xapian::termcount get_max_partial_expansion() const {
	return qpi->max_partial_expansion;
    }

    int get_max_partial_type() const {
	return qpi->max_partial_type;
    }
};

string
Term::make_term(const string & prefix) const
{
    string term;
    if (stem != QueryParser::STEM_NONE && stem != QueryParser::STEM_ALL)
	term += 'Z';
    if (!prefix.empty()) {
	term += prefix;
	if (prefix_needs_colon(prefix, name[0])) term += ':';
    }
    if (stem != QueryParser::STEM_NONE) {
	term += state->stem_term(name);
    } else {
	term += name;
    }

    if (!unstemmed.empty())
	state->add_to_unstem(term, unstemmed);
    return term;
}

// Iterator shim to allow building a synonym query from a TermIterator pair.
class SynonymIterator {
    Xapian::TermIterator i;

    Xapian::termpos pos;

    const Xapian::Query * first;

  public:
    SynonymIterator(const Xapian::TermIterator & i_,
		    Xapian::termpos pos_ = 0,
		    const Xapian::Query * first_ = NULL)
	: i(i_), pos(pos_), first(first_) { }

    SynonymIterator & operator++() {
	if (first)
	    first = NULL;
	else
	    ++i;
	return *this;
    }

    const Xapian::Query operator*() const {
	if (first) return *first;
	return Xapian::Query(*i, 1, pos);
    }

    bool operator==(const SynonymIterator & o) const {
	return i == o.i && first == o.first;
    }

    bool operator!=(const SynonymIterator & o) const {
	return !(*this == o);
    }

    typedef std::input_iterator_tag iterator_category;
    typedef Xapian::Query value_type;
    typedef Xapian::termcount_diff difference_type;
    typedef Xapian::Query * pointer;
    typedef Xapian::Query & reference;
};

Query
Term::get_query_with_synonyms() const
{
    // Handle single-word synonyms with each prefix.
    const auto& prefixes = field_info->prefixes;
    if (prefixes.empty()) {
	Assert(field_info->proc.get());
	return (*field_info->proc)(name);
    }

    Query q = get_query();

    for (auto&& prefix : prefixes) {
	// First try the unstemmed term:
	string term;
	if (!prefix.empty()) {
	    term += prefix;
	    if (prefix_needs_colon(prefix, name[0])) term += ':';
	}
	term += name;

	Xapian::Database db = state->get_database();
	Xapian::TermIterator syn = db.synonyms_begin(term);
	Xapian::TermIterator end = db.synonyms_end(term);
	if (syn == end && stem != QueryParser::STEM_NONE) {
	    // If that has no synonyms, try the stemmed form:
	    term = 'Z';
	    if (!prefix.empty()) {
		term += prefix;
		if (prefix_needs_colon(prefix, name[0])) term += ':';
	    }
	    term += state->stem_term(name);
	    syn = db.synonyms_begin(term);
	    end = db.synonyms_end(term);
	}
	q = Query(q.OP_SYNONYM,
		  SynonymIterator(syn, pos, &q),
		  SynonymIterator(end));
    }
    return q;
}

Query
Term::get_query_with_auto_synonyms() const
{
    const unsigned MASK_ENABLE_AUTO_SYNONYMS =
	QueryParser::FLAG_AUTO_SYNONYMS |
	QueryParser::FLAG_AUTO_MULTIWORD_SYNONYMS;
    if (state->flags & MASK_ENABLE_AUTO_SYNONYMS)
	return get_query_with_synonyms();

    return get_query();
}

static void
add_to_query(Query *& q, Query::op op, Query * term)
{
    Assert(term);
    if (q) {
	if (op == Query::OP_OR) {
	    *q |= *term;
	} else if (op == Query::OP_AND) {
	    *q &= *term;
	} else {
	    *q = Query(op, *q, *term);
	}
	delete term;
    } else {
	q = term;
    }
}

static void
add_to_query(Query *& q, Query::op op, const Query & term)
{
    if (q) {
	if (op == Query::OP_OR) {
	    *q |= term;
	} else if (op == Query::OP_AND) {
	    *q &= term;
	} else {
	    *q = Query(op, *q, term);
	}
    } else {
	q = new Query(term);
    }
}

Query
Term::get_query() const
{
    const auto& prefixes = field_info->prefixes;
    if (prefixes.empty()) {
	Assert(field_info->proc.get());
	return (*field_info->proc)(name);
    }
    auto piter = prefixes.begin();
    Query q(make_term(*piter), 1, pos);
    while (++piter != prefixes.end()) {
	q |= Query(make_term(*piter), 1, pos);
    }
    return q;
}

Query *
Term::as_wildcarded_query(State * state_) const
{
    const auto& prefixes = field_info->prefixes;
    Xapian::termcount max = state_->get_max_wildcard_expansion();
    int max_type = state_->get_max_wildcard_type();
    vector<Query> subqs;
    subqs.reserve(prefixes.size());
    for (string root : prefixes) {
	root += name;
	// Combine with OP_OR, and apply OP_SYNONYM afterwards.
	subqs.push_back(Query(Query::OP_WILDCARD, root, max, max_type,
			      Query::OP_OR));
    }
    Query * q = new Query(Query::OP_SYNONYM, subqs.begin(), subqs.end());
    delete this;
    return q;
}

Query *
Term::as_partial_query(State * state_) const
{
    Xapian::termcount max = state_->get_max_partial_expansion();
    int max_type = state_->get_max_partial_type();
    vector<Query> subqs_partial; // A synonym of all the partial terms.
    vector<Query> subqs_full; // A synonym of all the full terms.

    for (const string& prefix : field_info->prefixes) {
	string root = prefix;
	root += name;
	// Combine with OP_OR, and apply OP_SYNONYM afterwards.
	subqs_partial.push_back(Query(Query::OP_WILDCARD, root, max, max_type,
				      Query::OP_OR));
	// Add the term, as it would normally be handled, as an alternative.
	subqs_full.push_back(Query(make_term(prefix), 1, pos));
    }
    Query * q = new Query(Query::OP_OR,
			  Query(Query::OP_SYNONYM,
				subqs_partial.begin(), subqs_partial.end()),
			  Query(Query::OP_SYNONYM,
				subqs_full.begin(), subqs_full.end()));
    delete this;
    return q;
}

Query *
Term::as_unbroken_query() const
{
    vector<Query> prefix_subqs;
    vector<Query> ngram_subqs;
    const auto& prefixes = field_info->prefixes;
    for (const string& prefix : prefixes) {
	for (NgramIterator tk(name); tk != NgramIterator(); ++tk) {
	    ngram_subqs.push_back(Query(prefix + *tk, 1, pos));
	}
	prefix_subqs.push_back(Query(Query::OP_AND,
				     ngram_subqs.begin(), ngram_subqs.end()));
	ngram_subqs.clear();
    }
    Query * q = new Query(Query::OP_OR,
			  prefix_subqs.begin(), prefix_subqs.end());
    delete this;
    return q;
}

Query
Term::as_range_query() const
{
    Query q = query;
    delete this;
    return q;
}

inline bool
is_phrase_generator(unsigned ch)
{
    // These characters generate a phrase search.
    // Ordered mostly by frequency of calls to this function done when
    // running the testcases in api_queryparser.cc.
    return (ch && ch < 128 && strchr(".-/:\\@", ch) != NULL);
}

inline bool
is_stem_preventer(unsigned ch)
{
    return (ch && ch < 128 && strchr("(/\\@<>=*[{\"", ch) != NULL);
}

inline bool
should_stem(const string & term)
{
    const unsigned int SHOULD_STEM_MASK =
	(1 << Unicode::LOWERCASE_LETTER) |
	(1 << Unicode::TITLECASE_LETTER) |
	(1 << Unicode::MODIFIER_LETTER) |
	(1 << Unicode::OTHER_LETTER);
    Utf8Iterator u(term);
    return ((SHOULD_STEM_MASK >> Unicode::get_category(*u)) & 1);
}

/** Value representing "ignore this" when returned by check_infix() or
 *  check_infix_digit().
 */
const unsigned UNICODE_IGNORE = numeric_limits<unsigned>::max();

inline unsigned check_infix(unsigned ch) {
    if (ch == '\'' || ch == '&' || ch == 0xb7 || ch == 0x5f4 || ch == 0x2027) {
	// Unicode includes all these except '&' in its word boundary rules,
	// as well as 0x2019 (which we handle below) and ':' (for Swedish
	// apparently, but we ignore this for now as it's problematic in
	// real world cases).
	return ch;
    }
    if (ch >= 0x200b) {
	// 0x2019 is Unicode apostrophe and single closing quote.
	// 0x201b is Unicode single opening quote with the tail rising.
	if (ch == 0x2019 || ch == 0x201b)
	    return '\'';
	if (ch <= 0x200d || ch == 0x2060 || ch == 0xfeff)
	    return UNICODE_IGNORE;
    }
    return 0;
}

inline unsigned check_infix_digit(unsigned ch) {
    // This list of characters comes from Unicode's word identifying algorithm.
    switch (ch) {
	case ',':
	case '.':
	case ';':
	case 0x037e: // GREEK QUESTION MARK
	case 0x0589: // ARMENIAN FULL STOP
	case 0x060D: // ARABIC DATE SEPARATOR
	case 0x07F8: // NKO COMMA
	case 0x2044: // FRACTION SLASH
	case 0xFE10: // PRESENTATION FORM FOR VERTICAL COMMA
	case 0xFE13: // PRESENTATION FORM FOR VERTICAL COLON
	case 0xFE14: // PRESENTATION FORM FOR VERTICAL SEMICOLON
	    return ch;
    }
    if (ch >= 0x200b && (ch <= 0x200d || ch == 0x2060 || ch == 0xfeff))
	return UNICODE_IGNORE;
    return 0;
}

// Prototype a function lemon generates, but which we want to call before that
// in the generated source code file.
struct yyParser;
static void yy_parse_failed(yyParser *);

void
QueryParser::Internal::add_prefix(const string &field, const string &prefix)
{
    map<string, FieldInfo>::iterator p = field_map.find(field);
    if (p == field_map.end()) {
	field_map.insert(make_pair(field, FieldInfo(NON_BOOLEAN, prefix)));
    } else {
	// Check that this is the same type of filter as the existing one(s).
	if (p->second.type != NON_BOOLEAN) {
	    throw Xapian::InvalidOperationError("Can't use add_prefix() and add_boolean_prefix() on the same field name, or add_boolean_prefix() with different values of the 'exclusive' parameter");
	}
	if (p->second.proc.get())
	    throw Xapian::FeatureUnavailableError("Mixing FieldProcessor objects and string prefixes currently not supported");
	p->second.prefixes.push_back(prefix);
   }
}

void
QueryParser::Internal::add_prefix(const string &field, FieldProcessor *proc)
{
    map<string, FieldInfo>::iterator p = field_map.find(field);
    if (p == field_map.end()) {
	field_map.insert(make_pair(field, FieldInfo(NON_BOOLEAN, proc)));
    } else {
	// Check that this is the same type of filter as the existing one(s).
	if (p->second.type != NON_BOOLEAN) {
	    throw Xapian::InvalidOperationError("Can't use add_prefix() and add_boolean_prefix() on the same field name, or add_boolean_prefix() with different values of the 'exclusive' parameter");
	}
	if (!p->second.prefixes.empty())
	    throw Xapian::FeatureUnavailableError("Mixing FieldProcessor objects and string prefixes currently not supported");
	throw Xapian::FeatureUnavailableError("Multiple FieldProcessor objects for the same prefix currently not supported");
   }
}

void
QueryParser::Internal::add_boolean_prefix(const string &field,
					  const string &prefix,
					  const string* grouping)
{
    // Don't allow the empty prefix to be set as boolean as it doesn't
    // really make sense.
    if (field.empty())
	throw Xapian::UnimplementedError("Can't set the empty prefix to be a boolean filter");
    if (!grouping) grouping = &field;
    filter_type type = grouping->empty() ? BOOLEAN : BOOLEAN_EXCLUSIVE;
    map<string, FieldInfo>::iterator p = field_map.find(field);
    if (p == field_map.end()) {
	field_map.insert(make_pair(field, FieldInfo(type, prefix, *grouping)));
    } else {
	// Check that this is the same type of filter as the existing one(s).
	if (p->second.type != type) {
	    throw Xapian::InvalidOperationError("Can't use add_prefix() and add_boolean_prefix() on the same field name, or add_boolean_prefix() with different values of the 'exclusive' parameter"); // FIXME
	}
	if (p->second.proc.get())
	    throw Xapian::FeatureUnavailableError("Mixing FieldProcessor objects and string prefixes currently not supported");
	p->second.prefixes.push_back(prefix); // FIXME grouping
   }
}

void
QueryParser::Internal::add_boolean_prefix(const string &field,
					  FieldProcessor *proc,
					  const string* grouping)
{
    // Don't allow the empty prefix to be set as boolean as it doesn't
    // really make sense.
    if (field.empty())
	throw Xapian::UnimplementedError("Can't set the empty prefix to be a boolean filter");
    if (!grouping) grouping = &field;
    filter_type type = grouping->empty() ? BOOLEAN : BOOLEAN_EXCLUSIVE;
    map<string, FieldInfo>::iterator p = field_map.find(field);
    if (p == field_map.end()) {
	field_map.insert(make_pair(field, FieldInfo(type, proc, *grouping)));
    } else {
	// Check that this is the same type of filter as the existing one(s).
	if (p->second.type != type) {
	    throw Xapian::InvalidOperationError("Can't use add_prefix() and add_boolean_prefix() on the same field name, or add_boolean_prefix() with different values of the 'exclusive' parameter"); // FIXME
	}
	if (!p->second.prefixes.empty())
	    throw Xapian::FeatureUnavailableError("Mixing FieldProcessor objects and string prefixes currently not supported");
	throw Xapian::FeatureUnavailableError("Multiple FieldProcessor objects for the same prefix currently not supported");
   }
}

string
QueryParser::Internal::parse_term(Utf8Iterator &it, const Utf8Iterator &end,
				  bool try_word_break, bool& needs_word_break,
				  bool &was_acronym)
{
    string term;
    // Look for initials separated by '.' (e.g. P.T.O., U.N.C.L.E).
    // Don't worry if there's a trailing '.' or not.
    if (U_isupper(*it)) {
	string t;
	Utf8Iterator p = it;
	do {
	    Unicode::append_utf8(t, *p++);
	} while (p != end && *p == '.' && ++p != end && U_isupper(*p));
	// One letter does not make an acronym!  If we handled a single
	// uppercase letter here, we wouldn't catch M&S below.
	if (t.length() > 1) {
	    // Check there's not a (lower case) letter or digit
	    // immediately after it.
	    // FIXME: should I.B.M..P.T.O be a range search?
	    if (p == end || !is_wordchar(*p)) {
		it = p;
		swap(term, t);
	    }
	}
    }
    was_acronym = !term.empty();

    if (try_word_break && term.empty() && is_unbroken_script(*it)) {
	const char* start = it.raw();
	get_unbroken(it);
	term.assign(start, it.raw() - start);
	needs_word_break = true;
    }

    if (term.empty()) {
	unsigned prevch = *it;
	Unicode::append_utf8(term, prevch);
	while (++it != end) {
	    if (try_word_break && is_unbroken_script(*it)) break;
	    unsigned ch = *it;
	    if (!is_wordchar(ch)) {
		// Treat a single embedded '&' or "'" or similar as a word
		// character (e.g. AT&T, Fred's).  Also, normalise
		// apostrophes to ASCII apostrophe.
		Utf8Iterator p = it;
		++p;
		if (p == end || !is_wordchar(*p)) break;
		unsigned nextch = *p;
		if (is_digit(prevch) && is_digit(nextch)) {
		    ch = check_infix_digit(ch);
		} else {
		    ch = check_infix(ch);
		}
		if (!ch) break;
		if (ch == UNICODE_IGNORE)
		    continue;
	    }
	    Unicode::append_utf8(term, ch);
	    prevch = ch;
	}
	if (it != end && is_suffix(*it)) {
	    string suff_term = term;
	    Utf8Iterator p = it;
	    // Keep trailing + (e.g. C++, Na+) or # (e.g. C#).
	    do {
		if (suff_term.size() - term.size() == 3) {
		    suff_term.resize(0);
		    break;
		}
		suff_term += *p;
	    } while (is_suffix(*++p));
	    if (!suff_term.empty() && (p == end || !is_wordchar(*p))) {
		// If the suffixed term doesn't exist, check that the
		// non-suffixed term does.  This also takes care of
		// the case when QueryParser::set_database() hasn't
		// been called.
		bool use_suff_term = false;
		string lc = Unicode::tolower(suff_term);
		if (db.term_exists(lc)) {
		    use_suff_term = true;
		} else {
		    lc = Unicode::tolower(term);
		    if (!db.term_exists(lc)) use_suff_term = true;
		}
		if (use_suff_term) {
		    term = suff_term;
		    it = p;
		}
	    }
	}
    }
    return term;
}

#line 1416 "queryparser/queryparser.lemony"


struct ProbQuery {
    Query* query = NULL;
    Query* love = NULL;
    Query* hate = NULL;
    // filter is a map from prefix to a query for that prefix.  Queries with
    // the same prefix are combined with OR, and the results of this are
    // combined with AND to get the full filter.
    map<string, Query> filter;

    ProbQuery() {}

    explicit
    ProbQuery(Query* query_) : query(query_) {}

    ~ProbQuery() {
	delete query;
	delete love;
	delete hate;
    }

    void add_filter(const string& grouping, const Query & q) {
	filter[grouping] = q;
    }

    void append_filter(const string& grouping, const Query & qnew) {
	auto it = filter.find(grouping);
	if (it == filter.end()) {
	    filter.insert(make_pair(grouping, qnew));
	} else {
	    Query & q = it->second;
	    // We OR multiple filters with the same prefix if they're
	    // exclusive, otherwise we AND them.
	    bool exclusive = !grouping.empty();
	    if (exclusive) {
		q |= qnew;
	    } else {
		q &= qnew;
	    }
	}
    }

    void add_filter_range(const string& grouping, const Query & range) {
	filter[grouping] = range;
    }

    void append_filter_range(const string& grouping, const Query & range) {
	Query & q = filter[grouping];
	q |= range;
    }

    Query merge_filters() const {
	auto i = filter.begin();
	Assert(i != filter.end());
	Query q = i->second;
	while (++i != filter.end()) {
	    q &= i->second;
	}
	return q;
    }
};

/// A group of terms separated only by whitespace.
class TermGroup {
    vector<Term *> terms;

    /** Controls how to handle a group where all terms are stopwords.
     *
     *  If true, then as_group() returns NULL.  If false, then the
     *  stopword status of the terms is ignored.
     */
    bool empty_ok;

    TermGroup(Term* t1, Term* t2) : empty_ok(false) {
	add_term(t1);
	add_term(t2);
    }

  public:
    /// Factory function - ensures heap allocation.
    static TermGroup* create(Term* t1, Term* t2) {
	return new TermGroup(t1, t2);
    }

    ~TermGroup() {
	for (auto&& t : terms) {
	    delete t;
	}
    }

    /// Add a Term object to this TermGroup object.
    void add_term(Term * term) {
	terms.push_back(term);
    }

    /// Set the empty_ok flag.
    void set_empty_ok() { empty_ok = true; }

    /// Convert to a Xapian::Query * using default_op.
    Query * as_group(State *state) const;
};

Query *
TermGroup::as_group(State *state) const
{
    const Xapian::Stopper * stopper = state->get_stopper();
    size_t stoplist_size = state->stoplist_size();
    bool default_op_is_positional = is_positional(state->default_op());
reprocess:
    Query::op default_op = state->default_op();
    vector<Query> subqs;
    subqs.reserve(terms.size());
    if (state->flags & QueryParser::FLAG_AUTO_MULTIWORD_SYNONYMS) {
	// Check for multi-word synonyms.
	Database db = state->get_database();

	string key;
	vector<Term*>::size_type begin = 0;
	vector<Term*>::size_type i = begin;
	while (terms.size() - i > 0) {
	    size_t longest_match = 0;
	    // This value is never used, but GCC 4.8 warns with
	    // -Wmaybe-uninitialized (GCC 5.4 doesn't).
	    vector<Term*>::size_type longest_match_end = 0;
	    if (terms.size() - i >= 2) {
		// Greedily try to match as many consecutive words as possible.
		key = terms[i]->name;
		key += ' ';
		key += terms[i + 1]->name;
		TermIterator synkey(db.synonym_keys_begin(key));
		TermIterator synend(db.synonym_keys_end(key));
		if (synkey != synend) {
		    longest_match = key.size();
		    longest_match_end = i + 2;
		    for (auto j = i + 2; j < terms.size(); ++j) {
			key += ' ';
			key += terms[j]->name;
			synkey.skip_to(key);
			if (synkey == synend)
			    break;
			const string& found = *synkey;
			if (!startswith(found, key))
			    break;
			if (found.size() == key.size()) {
			    longest_match = key.size();
			    longest_match_end = j + 1;
			}
		    }
		}
	    }
	    if (longest_match == 0) {
		// No multi-synonym matches at position i.
		if (stopper && (*stopper)(terms[i]->name)) {
		    state->add_to_stoplist(terms[i]);
		} else {
		    if (default_op_is_positional)
			terms[i]->need_positions();
		    subqs.push_back(terms[i]->get_query_with_auto_synonyms());
		}
		begin = ++i;
		continue;
	    }
	    i = longest_match_end;
	    key.resize(longest_match);

	    vector<Query> subqs2;
	    for (auto j = begin; j != i; ++j) {
		if (stopper && (*stopper)(terms[j]->name)) {
		    state->add_to_stoplist(terms[j]);
		} else {
		    if (default_op_is_positional)
			terms[i]->need_positions();
		    subqs2.push_back(terms[j]->get_query());
		}
	    }
	    Query q_original_terms;
	    if (default_op_is_positional) {
		q_original_terms = Query(default_op,
					 subqs2.begin(), subqs2.end(),
					 subqs2.size() + 9);
	    } else {
		q_original_terms = Query(default_op,
					 subqs2.begin(), subqs2.end());
	    }
	    subqs2.clear();

	    // Use the position of the first term for the synonyms.
	    TermIterator syn = db.synonyms_begin(key);
	    Query q(Query::OP_SYNONYM,
		    SynonymIterator(syn, terms[begin]->pos, &q_original_terms),
		    SynonymIterator(db.synonyms_end(key)));
	    subqs.push_back(q);

	    begin = i;
	}
    } else {
	vector<Term*>::const_iterator i;
	for (i = terms.begin(); i != terms.end(); ++i) {
	    if (stopper && (*stopper)((*i)->name)) {
		state->add_to_stoplist(*i);
	    } else {
		if (default_op_is_positional)
		    (*i)->need_positions();
		subqs.push_back((*i)->get_query_with_auto_synonyms());
	    }
	}
    }

    if (!empty_ok && stopper && subqs.empty() &&
	stoplist_size < state->stoplist_size()) {
	// This group is all stopwords, so roll-back, disable stopper
	// temporarily, and reprocess this group.
	state->stoplist_resize(stoplist_size);
	stopper = NULL;
	goto reprocess;
    }

    Query * q = NULL;
    if (!subqs.empty()) {
	if (default_op_is_positional) {
	    q = new Query(default_op, subqs.begin(), subqs.end(),
			     subqs.size() + 9);
	} else {
	    q = new Query(default_op, subqs.begin(), subqs.end());
	}
    }
    delete this;
    return q;
}

/// Some terms which form a positional sub-query.
class Terms {
    vector<Term *> terms;

    /** Window size.
     *
     *  size_t(-1) means don't use positional info (so an OP_AND query gets
     *  created).
     */
    size_t window;

    /** Keep track of whether the terms added all have the same list of
     *  prefixes.  If so, we'll build a set of phrases, one using each prefix.
     *  This works around the limitation that a phrase cannot have multiple
     *  components which are "OR" combinations of terms, but is also probably
     *  what users expect: i.e., if a user specifies a phrase in a field, and
     *  that field maps to multiple prefixes, the user probably wants a phrase
     *  returned with all terms having one of those prefixes, rather than a
     *  phrase comprised of terms with differing prefixes.
     */
    bool uniform_prefixes;

    /** The list of prefixes of the terms added.
     *  This will be NULL if the terms have different prefixes.
     */
    const vector<string>* prefixes;

    Query opwindow_subq(Query::op op,
			const vector<Query>& v,
			Xapian::termcount w) const {
	if (op == Query::OP_AND) {
	    return Query(op, v.begin(), v.end());
	}
	return Query(op, v.begin(), v.end(), w);
    }

    /// Convert to a query using the given operator and window size.
    Query * as_opwindow_query(Query::op op, Xapian::termcount w_delta) const {
	if (window == size_t(-1)) op = Query::OP_AND;
	Query * q = NULL;
	size_t n_terms = terms.size();
	Xapian::termcount w = w_delta + terms.size();
	if (uniform_prefixes) {
	    if (prefixes) {
		for (auto&& prefix : *prefixes) {
		    vector<Query> subqs;
		    subqs.reserve(n_terms);
		    for (Term* t : terms) {
			subqs.push_back(Query(t->make_term(prefix), 1, t->pos));
		    }
		    add_to_query(q, Query::OP_OR, opwindow_subq(op, subqs, w));
		}
	    }
	} else {
	    vector<Query> subqs;
	    subqs.reserve(n_terms);
	    for (Term* t : terms) {
		subqs.push_back(t->get_query());
	    }
	    q = new Query(opwindow_subq(op, subqs, w));
	}

	delete this;
	return q;
    }

    explicit Terms(bool no_pos)
	: window(no_pos ? size_t(-1) : 0),
	  uniform_prefixes(true),
	  prefixes(NULL) { }

  public:
    /// Factory function - ensures heap allocation.
    static Terms* create(State* state) {
	return new Terms(state->flags & QueryParser::FLAG_NO_POSITIONS);
    }

    ~Terms() {
	for (auto&& t : terms) {
	    delete t;
	}
    }

    /// Add an unstemmed Term object to this Terms object.
    void add_positional_term(Term * term) {
	const auto& term_prefixes = term->field_info->prefixes;
	if (terms.empty()) {
	    prefixes = &term_prefixes;
	} else if (uniform_prefixes && prefixes != &term_prefixes) {
	    if (*prefixes != term_prefixes)  {
		prefixes = NULL;
		uniform_prefixes = false;
	    }
	}
	term->need_positions();
	terms.push_back(term);
    }

    void adjust_window(size_t alternative_window) {
	if (alternative_window > window) window = alternative_window;
    }

    /// Convert to a Xapian::Query * using adjacent OP_PHRASE.
    Query * as_phrase_query() const {
	return as_opwindow_query(Query::OP_PHRASE, 0);
    }

    /// Convert to a Xapian::Query * using OP_NEAR.
    Query * as_near_query() const {
	// The common meaning of 'a NEAR b' is "a within 10 terms of b", which
	// means a window size of 11.  For more than 2 terms, we just add one
	// to the window size for each extra term.
	size_t w = window;
	if (w == 0) w = 10;
	return as_opwindow_query(Query::OP_NEAR, w - 1);
    }

    /// Convert to a Xapian::Query * using OP_PHRASE to implement ADJ.
    Query * as_adj_query() const {
	// The common meaning of 'a ADJ b' is "a at most 10 terms before b",
	// which means a window size of 11.  For more than 2 terms, we just add
	// one to the window size for each extra term.
	size_t w = window;
	if (w == 0) w = 10;
	return as_opwindow_query(Query::OP_PHRASE, w - 1);
    }
};

void
Term::as_positional_unbroken(Terms* terms) const
{
    // Add each individual character to the phrase.
    string t;
    for (Utf8Iterator it(name); it != Utf8Iterator(); ++it) {
	Unicode::append_utf8(t, *it);
	Term * c = new Term(state, t, field_info, unstemmed, stem, pos);
	terms->add_positional_term(c);
	t.resize(0);
    }

    // FIXME: we want to add the n-grams as filters too for efficiency.

    delete this;
}

// Helper macro to check for missing arguments to a boolean operator.
#define VET_BOOL_ARGS(A, B, OP_TXT) \
    do {\
	if (!A || !B) {\
	    state->error = "Syntax: <expression> " OP_TXT " <expression>";\
	    yy_parse_failed(yypParser);\
	    return;\
	}\
    } while (0)

#line 1213 "queryparser/queryparser_internal.cc"
/**************** End of %include directives **********************************/
/* These constants specify the various numeric values for terminal symbols
** in a format understandable to "makeheaders".  This section is blank unless
** "lemon" is run with the "-m" command-line option.
***************** Begin makeheaders token definitions *************************/
/**************** End makeheaders token definitions ***************************/

/* The next section is a series of control #defines.
** various aspects of the generated parser.
**    YYCODETYPE         is the data type used to store the integer codes
**                       that represent terminal and non-terminal symbols.
**                       "unsigned char" is used if there are fewer than
**                       256 symbols.  Larger types otherwise.
**    YYNOCODE           is a number of type YYCODETYPE that is not used for
**                       any terminal or nonterminal symbol.
**    YYFALLBACK         If defined, this indicates that one or more tokens
**                       (also known as: "terminal symbols") have fall-back
**                       values which should be used if the original symbol
**                       would not parse.  This permits keywords to sometimes
**                       be used as identifiers, for example.
**    YYACTIONTYPE       is the data type used for "action codes" - numbers
**                       that indicate what to do in response to the next
**                       token.
**    ParseTOKENTYPE     is the data type used for minor type for terminal
**                       symbols.  Background: A "minor type" is a semantic
**                       value associated with a terminal or non-terminal
**                       symbols.  For example, for an "ID" terminal symbol,
**                       the minor type might be the name of the identifier.
**                       Each non-terminal can have a different minor type.
**                       Terminal symbols all have the same minor type, though.
**                       This macros defines the minor type for terminal 
**                       symbols.
**    YYMINORTYPE        is the data type used for all minor types.
**                       This is typically a union of many types, one of
**                       which is ParseTOKENTYPE.  The entry in the union
**                       for terminal symbols is called "yy0".
**    YYSTACKDEPTH       is the maximum depth of the parser's stack.  If
**                       zero the stack is dynamically sized using realloc()
**    ParseARG_SDECL     A static variable declaration for the %extra_argument
**    ParseARG_PDECL     A parameter declaration for the %extra_argument
**    ParseARG_STORE     Code to store %extra_argument into yypParser
**    ParseARG_FETCH     Code to extract %extra_argument from yypParser
**    YYERRORSYMBOL      is the code number of the error symbol.  If not
**                       defined, then do no error processing.
**    YYNSTATE           the combined number of states.
**    YYNRULE            the number of rules in the grammar
**    YYNTOKEN           Number of terminal symbols
**    YY_MAX_SHIFT       Maximum value for shift actions
**    YY_MIN_SHIFTREDUCE Minimum value for shift-reduce actions
**    YY_MAX_SHIFTREDUCE Maximum value for shift-reduce actions
**    YY_ERROR_ACTION    The yy_action[] code for syntax error
**    YY_ACCEPT_ACTION   The yy_action[] code for accept
**    YY_NO_ACTION       The yy_action[] code for no-op
**    YY_MIN_REDUCE      Minimum value for reduce actions
**    YY_MAX_REDUCE      Maximum value for reduce actions
*/
#ifndef INTERFACE
# define INTERFACE 1
#endif
/************* Begin control #defines *****************************************/
#define YYCODETYPE unsigned char
#define YYNOCODE 40
#define YYACTIONTYPE unsigned char
#define ParseTOKENTYPE Term *
typedef union {
  int yyinit;
  ParseTOKENTYPE yy0;
  TermGroup * yy14;
  Terms * yy32;
  Query * yy39;
  ProbQuery * yy40;
  int yy46;
} YYMINORTYPE;
#ifndef YYSTACKDEPTH
#define YYSTACKDEPTH 100
#endif
#define ParseARG_SDECL State * state;
#define ParseARG_PDECL ,State * state
#define ParseARG_FETCH State * state = yypParser->state
#define ParseARG_STORE yypParser->state = state
#define YYNSTATE             35
#define YYNRULE              56
#define YYNTOKEN             24
#define YY_MAX_SHIFT         34
#define YY_MIN_SHIFTREDUCE   77
#define YY_MAX_SHIFTREDUCE   132
#define YY_ERROR_ACTION      133
#define YY_ACCEPT_ACTION     134
#define YY_NO_ACTION         135
#define YY_MIN_REDUCE        136
#define YY_MAX_REDUCE        191
/************* End control #defines *******************************************/

/* Define the yytestcase() macro to be a no-op if is not already defined
** otherwise.
**
** Applications can choose to define yytestcase() in the %include section
** to a macro that can assist in verifying code coverage.  For production
** code the yytestcase() macro should be turned off.  But it is useful
** for testing.
*/
#ifndef yytestcase
# define yytestcase(X)
#endif


/* Next are the tables used to determine what action to take based on the
** current state and lookahead token.  These tables are used to implement
** functions that take a state number and lookahead value and return an
** action integer.  
**
** Suppose the action integer is N.  Then the action is determined as
** follows
**
**   0 <= N <= YY_MAX_SHIFT             Shift N.  That is, push the lookahead
**                                      token onto the stack and goto state N.
**
**   N between YY_MIN_SHIFTREDUCE       Shift to an arbitrary state then
**     and YY_MAX_SHIFTREDUCE           reduce by rule N-YY_MIN_SHIFTREDUCE.
**
**   N == YY_ERROR_ACTION               A syntax error has occurred.
**
**   N == YY_ACCEPT_ACTION              The parser accepts its input.
**
**   N == YY_NO_ACTION                  No such action.  Denotes unused
**                                      slots in the yy_action[] table.
**
**   N between YY_MIN_REDUCE            Reduce by rule N-YY_MIN_REDUCE
**     and YY_MAX_REDUCE
**
** The action table is constructed as a single large table named yy_action[].
** Given state S and lookahead X, the action is computed as either:
**
**    (A)   N = yy_action[ yy_shift_ofst[S] + X ]
**    (B)   N = yy_default[S]
**
** The (A) formula is preferred.  The B formula is used instead if
** yy_lookahead[yy_shift_ofst[S]+X] is not equal to X.
**
** The formulas above are for computing the action when the lookahead is
** a terminal symbol.  If the lookahead is a non-terminal (as occurs after
** a reduce action) then the yy_reduce_ofst[] array is used in place of
** the yy_shift_ofst[] array.
**
** The following are the tables generated in this section:
**
**  yy_action[]        A single table containing all actions.
**  yy_lookahead[]     A table containing the lookahead for each entry in
**                     yy_action.  Used to detect hash collisions.
**  yy_shift_ofst[]    For each state, the offset into yy_action for
**                     shifting terminals.
**  yy_reduce_ofst[]   For each state, the offset into yy_action for
**                     shifting non-terminals after a reduce.
**  yy_default[]       Default action for each state.
**
*********** Begin parsing tables **********************************************/
#define YY_ACTTAB_COUNT (326)
static const YYACTIONTYPE yy_action[] = {
 /*     0 */   134,   34,   34,   20,    8,   34,   18,   13,   16,   27,
 /*    10 */    31,   23,   30,   28,    3,   21,  112,   10,    9,    2,
 /*    20 */    25,   15,  111,  114,  104,  105,   97,   87,   14,    4,
 /*    30 */   137,  113,  126,  115,   12,   11,    1,    7,   10,    9,
 /*    40 */   124,   25,   15,   98,   88,  104,  105,   97,   87,   14,
 /*    50 */     4,   29,  113,  138,  138,  138,    8,  138,   18,   13,
 /*    60 */    16,  119,   31,   23,   30,   28,  141,  141,  141,    8,
 /*    70 */   141,   18,   13,   16,  125,   31,   23,   30,   28,  140,
 /*    80 */   140,  140,    8,  140,   18,   13,   16,  123,   31,   23,
 /*    90 */    30,   28,   26,   26,   20,    8,   26,   18,   13,   16,
 /*   100 */   136,   31,   23,   30,   28,   24,   24,   24,    8,   24,
 /*   110 */    18,   13,   16,  135,   31,   23,   30,   28,   22,   22,
 /*   120 */    22,    8,   22,   18,   13,   16,  135,   31,   23,   30,
 /*   130 */    28,  139,  139,  139,    8,  139,   18,   13,   16,  121,
 /*   140 */    31,   23,   30,   28,   10,    9,  135,   25,   15,  122,
 /*   150 */   135,  104,  105,   97,   87,   14,    4,  135,  113,  135,
 /*   160 */   189,  189,  135,   25,   19,  135,  135,  104,  105,  189,
 /*   170 */   189,   14,    4,  162,  113,  162,  162,  162,  162,   33,
 /*   180 */    32,   33,   32,  116,  135,  135,  120,  118,  120,  118,
 /*   190 */   106,   25,   17,  117,  162,  104,  105,   95,  135,   14,
 /*   200 */     4,  135,  113,   25,   17,  135,  135,  104,  105,   99,
 /*   210 */   135,   14,    4,  135,  113,   25,   17,  135,  135,  104,
 /*   220 */   105,   96,  135,   14,    4,  135,  113,   25,   17,  135,
 /*   230 */   135,  104,  105,  100,  135,   14,    4,  135,  113,   25,
 /*   240 */    19,  135,  135,  104,  105,  135,  135,   14,    4,  135,
 /*   250 */   113,  135,  149,  149,  135,   31,   23,   30,   28,  152,
 /*   260 */   135,  135,  152,  135,   31,   23,   30,   28,  135,  150,
 /*   270 */   135,  135,  150,  135,   31,   23,   30,   28,  153,  135,
 /*   280 */   135,  153,  135,   31,   23,   30,   28,  151,  135,  135,
 /*   290 */   151,  135,   31,   23,   30,   28,  135,  148,  148,  135,
 /*   300 */    31,   23,   30,   28,  191,  135,  191,  191,  191,  191,
 /*   310 */     6,    5,    1,    7,    5,    1,    7,  135,  135,  135,
 /*   320 */   135,  135,  135,  135,  135,  191,
};
static const YYCODETYPE yy_lookahead[] = {
 /*     0 */    25,   26,   27,   28,   29,   30,   31,   32,   33,    7,
 /*    10 */    35,   36,   37,   38,    5,   34,   12,    8,    9,   10,
 /*    20 */    11,   12,   21,   12,   15,   16,   17,   18,   19,   20,
 /*    30 */     0,   22,   12,   22,    8,    9,    4,    5,    8,    9,
 /*    40 */    12,   11,   12,   17,   18,   15,   16,   17,   18,   19,
 /*    50 */    20,    6,   22,   26,   27,   28,   29,   30,   31,   32,
 /*    60 */    33,   14,   35,   36,   37,   38,   26,   27,   28,   29,
 /*    70 */    30,   31,   32,   33,   12,   35,   36,   37,   38,   26,
 /*    80 */    27,   28,   29,   30,   31,   32,   33,   12,   35,   36,
 /*    90 */    37,   38,   26,   27,   28,   29,   30,   31,   32,   33,
 /*   100 */     0,   35,   36,   37,   38,   26,   27,   28,   29,   30,
 /*   110 */    31,   32,   33,   39,   35,   36,   37,   38,   26,   27,
 /*   120 */    28,   29,   30,   31,   32,   33,   39,   35,   36,   37,
 /*   130 */    38,   26,   27,   28,   29,   30,   31,   32,   33,   13,
 /*   140 */    35,   36,   37,   38,    8,    9,   39,   11,   12,   23,
 /*   150 */    39,   15,   16,   17,   18,   19,   20,   39,   22,   39,
 /*   160 */     8,    9,   39,   11,   12,   39,   39,   15,   16,   17,
 /*   170 */    18,   19,   20,    0,   22,    2,    3,    4,    5,    6,
 /*   180 */     7,    6,    7,   12,   39,   39,   13,   14,   13,   14,
 /*   190 */    19,   11,   12,   22,   21,   15,   16,   17,   39,   19,
 /*   200 */    20,   39,   22,   11,   12,   39,   39,   15,   16,   17,
 /*   210 */    39,   19,   20,   39,   22,   11,   12,   39,   39,   15,
 /*   220 */    16,   17,   39,   19,   20,   39,   22,   11,   12,   39,
 /*   230 */    39,   15,   16,   17,   39,   19,   20,   39,   22,   11,
 /*   240 */    12,   39,   39,   15,   16,   39,   39,   19,   20,   39,
 /*   250 */    22,   39,   32,   33,   39,   35,   36,   37,   38,   30,
 /*   260 */    39,   39,   33,   39,   35,   36,   37,   38,   39,   30,
 /*   270 */    39,   39,   33,   39,   35,   36,   37,   38,   30,   39,
 /*   280 */    39,   33,   39,   35,   36,   37,   38,   30,   39,   39,
 /*   290 */    33,   39,   35,   36,   37,   38,   39,   32,   33,   39,
 /*   300 */    35,   36,   37,   38,    0,   39,    2,    3,    4,    5,
 /*   310 */     2,    3,    4,    5,    3,    4,    5,   39,   39,   39,
 /*   320 */    39,   39,   39,   39,   39,   21,   39,   39,   39,   39,
 /*   330 */    39,   39,   39,   39,   39,   39,   39,   39,   39,
};
#define YY_SHIFT_COUNT    (34)
#define YY_SHIFT_MIN      (0)
#define YY_SHIFT_MAX      (311)
static const unsigned short int yy_shift_ofst[] = {
 /*     0 */    30,    9,  136,  136,  136,  136,  136,  136,  152,  180,
 /*    10 */   192,  204,  216,  228,   11,  173,  304,  175,   26,  175,
 /*    20 */   308,  171,  311,  126,   32,    4,    1,   20,    2,   28,
 /*    30 */    45,   47,   62,   75,  100,
};
#define YY_REDUCE_COUNT (14)
#define YY_REDUCE_MIN   (-25)
#define YY_REDUCE_MAX   (265)
static const short yy_reduce_ofst[] = {
 /*     0 */   -25,   27,   40,   53,   66,   79,   92,  105,  220,  229,
 /*    10 */   239,  248,  257,  265,  -19,
};
static const YYACTIONTYPE yy_default[] = {
 /*     0 */   144,  144,  144,  144,  144,  144,  144,  144,  145,  133,
 /*    10 */   133,  133,  133,  160,  133,  161,  190,  162,  133,  161,
 /*    20 */   133,  133,  142,  167,  143,  133,  187,  133,  169,  133,
 /*    30 */   168,  166,  133,  133,  187,
};
/********** End of lemon-generated parsing tables *****************************/

/* The next table maps tokens (terminal symbols) into fallback tokens.  
** If a construct like the following:
** 
**      %fallback ID X Y Z.
**
** appears in the grammar, then ID becomes a fallback token for X, Y,
** and Z.  Whenever one of the tokens X, Y, or Z is input to the parser
** but it does not parse, the type of the token is changed to ID and
** the parse is retried before an error is thrown.
**
** This feature can be used, for example, to cause some keywords in a language
** to revert to identifiers if they keyword does not apply in the context where
** it appears.
*/
#ifdef YYFALLBACK
static const YYCODETYPE yyFallback[] = {
};
#endif /* YYFALLBACK */

/* The following structure represents a single element of the
** parser's stack.  Information stored includes:
**
**   +  The state number for the parser at this level of the stack.
**
**   +  The value of the token stored at this level of the stack.
**      (In other words, the "major" token.)
**
**   +  The semantic value stored at this level of the stack.  This is
**      the information used by the action routines in the grammar.
**      It is sometimes called the "minor" token.
**
** After the "shift" half of a SHIFTREDUCE action, the stateno field
** actually contains the reduce action for the second half of the
** SHIFTREDUCE.
*/
struct yyStackEntry {
  yyStackEntry() {
    stateno = 0;
    major = 0;
  }
  yyStackEntry(YYACTIONTYPE stateno_, YYCODETYPE major_, ParseTOKENTYPE minor_) {
    stateno = stateno_;
    major = major_;
    minor.yy0 = minor_;
  }
  YYACTIONTYPE stateno;  /* The state-number, or reduce action in SHIFTREDUCE */
  YYCODETYPE major;      /* The major token value.  This is the code
                         ** number for the token at this stack level */
  YYMINORTYPE minor;     /* The user-supplied minor token value.  This
                         ** is the value of the token  */
};

static void ParseInit(yyParser *pParser);
static void ParseFinalize(yyParser *pParser);

/* The state of the parser is completely contained in an instance of
** the following structure */
struct yyParser {
#ifdef YYTRACKMAXSTACKDEPTH
  int yyhwm;                    /* High-water mark of the stack */
#endif
#ifndef YYNOERRORRECOVERY
  int yyerrcnt;                 /* Shifts left before out of the error */
#endif
  ParseARG_SDECL                /* A place to hold %extra_argument */
  vector<yyStackEntry> yystack; /* The parser's stack */
  yyParser() {
    ParseInit(this);
  }
  ~yyParser() {
    ParseFinalize(this);
  }
};
typedef struct yyParser yyParser;

#include "omassert.h"
#include "debuglog.h"

#if defined(YYCOVERAGE) || defined(XAPIAN_DEBUG_LOG)
/* For tracing shifts, the names of all terminals and nonterminals
** are required.  The following table supplies these names */
static const char *const yyTokenName[] = { 
  /*    0 */ "$",
  /*    1 */ "ERROR",
  /*    2 */ "OR",
  /*    3 */ "XOR",
  /*    4 */ "AND",
  /*    5 */ "NOT",
  /*    6 */ "NEAR",
  /*    7 */ "ADJ",
  /*    8 */ "LOVE",
  /*    9 */ "HATE",
  /*   10 */ "HATE_AFTER_AND",
  /*   11 */ "SYNONYM",
  /*   12 */ "TERM",
  /*   13 */ "GROUP_TERM",
  /*   14 */ "PHR_TERM",
  /*   15 */ "WILD_TERM",
  /*   16 */ "PARTIAL_TERM",
  /*   17 */ "BOOLEAN_FILTER",
  /*   18 */ "RANGE",
  /*   19 */ "QUOTE",
  /*   20 */ "BRA",
  /*   21 */ "KET",
  /*   22 */ "UNBROKEN_WORDS",
  /*   23 */ "EMPTY_GROUP_OK",
  /*   24 */ "error",
  /*   25 */ "query",
  /*   26 */ "expr",
  /*   27 */ "prob_expr",
  /*   28 */ "bool_arg",
  /*   29 */ "prob",
  /*   30 */ "term",
  /*   31 */ "stop_prob",
  /*   32 */ "stop_term",
  /*   33 */ "compound_term",
  /*   34 */ "phrase",
  /*   35 */ "phrased_term",
  /*   36 */ "group",
  /*   37 */ "near_expr",
  /*   38 */ "adj_expr",
};

/* For tracing reduce actions, the names of all rules are required.
*/
static const char *const yyRuleName[] = {
 /*   0 */ "query ::= expr",
 /*   1 */ "query ::=",
 /*   2 */ "expr ::= bool_arg AND bool_arg",
 /*   3 */ "expr ::= bool_arg NOT bool_arg",
 /*   4 */ "expr ::= bool_arg AND NOT bool_arg",
 /*   5 */ "expr ::= bool_arg AND HATE_AFTER_AND bool_arg",
 /*   6 */ "expr ::= bool_arg OR bool_arg",
 /*   7 */ "expr ::= bool_arg XOR bool_arg",
 /*   8 */ "bool_arg ::=",
 /*   9 */ "prob_expr ::= prob",
 /*  10 */ "prob ::= RANGE",
 /*  11 */ "prob ::= stop_prob RANGE",
 /*  12 */ "prob ::= stop_term stop_term",
 /*  13 */ "prob ::= prob stop_term",
 /*  14 */ "prob ::= LOVE term",
 /*  15 */ "prob ::= stop_prob LOVE term",
 /*  16 */ "prob ::= HATE term",
 /*  17 */ "prob ::= stop_prob HATE term",
 /*  18 */ "prob ::= HATE BOOLEAN_FILTER",
 /*  19 */ "prob ::= stop_prob HATE BOOLEAN_FILTER",
 /*  20 */ "prob ::= BOOLEAN_FILTER",
 /*  21 */ "prob ::= stop_prob BOOLEAN_FILTER",
 /*  22 */ "prob ::= LOVE BOOLEAN_FILTER",
 /*  23 */ "prob ::= stop_prob LOVE BOOLEAN_FILTER",
 /*  24 */ "stop_prob ::= stop_term",
 /*  25 */ "stop_term ::= TERM",
 /*  26 */ "term ::= TERM",
 /*  27 */ "compound_term ::= WILD_TERM",
 /*  28 */ "compound_term ::= PARTIAL_TERM",
 /*  29 */ "compound_term ::= QUOTE phrase QUOTE",
 /*  30 */ "compound_term ::= phrased_term",
 /*  31 */ "compound_term ::= group",
 /*  32 */ "compound_term ::= near_expr",
 /*  33 */ "compound_term ::= adj_expr",
 /*  34 */ "compound_term ::= BRA expr KET",
 /*  35 */ "compound_term ::= SYNONYM TERM",
 /*  36 */ "compound_term ::= UNBROKEN_WORDS",
 /*  37 */ "phrase ::= TERM",
 /*  38 */ "phrase ::= UNBROKEN_WORDS",
 /*  39 */ "phrase ::= phrase TERM",
 /*  40 */ "phrase ::= phrase UNBROKEN_WORDS",
 /*  41 */ "phrased_term ::= TERM PHR_TERM",
 /*  42 */ "phrased_term ::= phrased_term PHR_TERM",
 /*  43 */ "group ::= TERM GROUP_TERM",
 /*  44 */ "group ::= group GROUP_TERM",
 /*  45 */ "group ::= group EMPTY_GROUP_OK",
 /*  46 */ "near_expr ::= TERM NEAR TERM",
 /*  47 */ "near_expr ::= near_expr NEAR TERM",
 /*  48 */ "adj_expr ::= TERM ADJ TERM",
 /*  49 */ "adj_expr ::= adj_expr ADJ TERM",
 /*  50 */ "expr ::= prob_expr",
 /*  51 */ "bool_arg ::= expr",
 /*  52 */ "prob_expr ::= term",
 /*  53 */ "stop_prob ::= prob",
 /*  54 */ "stop_term ::= compound_term",
 /*  55 */ "term ::= compound_term",
};

/*
** This function returns the symbolic name associated with a token
** value.
*/
static const char *ParseTokenName(int tokenType){
  if( tokenType>=0 && tokenType<(int)(sizeof(yyTokenName)/sizeof(yyTokenName[0])) ){
    return yyTokenName[tokenType];
  }
  return "Unknown";
}

/*
** This function returns the symbolic name associated with a rule
** value.
*/
static const char *ParseRuleName(int ruleNum){
  if( ruleNum>=0 && ruleNum<(int)(sizeof(yyRuleName)/sizeof(yyRuleName[0])) ){
    return yyRuleName[ruleNum];
  }
  return "Unknown";
}
#endif /* defined(YYCOVERAGE) || defined(XAPIAN_DEBUG_LOG) */

/* Datatype of the argument to the memory allocated passed as the
** second argument to ParseAlloc() below.  This can be changed by
** putting an appropriate #define in the %include section of the input
** grammar.
*/
#ifndef YYMALLOCARGTYPE
# define YYMALLOCARGTYPE size_t
#endif

/* Initialize a new parser that has already been allocated.
*/
static
void ParseInit(yyParser *pParser){
#ifdef YYTRACKMAXSTACKDEPTH
  pParser->yyhwm = 0;
#endif
#if 0
#if YYSTACKDEPTH<=0
  pParser->yytos = NULL;
  pParser->yystack = NULL;
  pParser->yystksz = 0;
  if( yyGrowStack(pParser) ){
    pParser->yystack = &pParser->yystk0;
    pParser->yystksz = 1;
  }
#endif
#endif
#ifndef YYNOERRORRECOVERY
  pParser->yyerrcnt = -1;
#endif
#if 0
  pParser->yytos = pParser->yystack;
  pParser->yystack[0].stateno = 0;
  pParser->yystack[0].major = 0;
#if YYSTACKDEPTH>0
  pParser->yystackEnd = &pParser->yystack[YYSTACKDEPTH-1];
#endif
#else
  pParser->yystack.push_back(yyStackEntry());
#endif
}

#ifndef Parse_ENGINEALWAYSONSTACK
/* 
** This function allocates a new parser.
**
** Inputs:
** None.
**
** Outputs:
** A pointer to a parser.  This pointer is used in subsequent calls
** to Parse and ParseFree.
*/
static yyParser *ParseAlloc(void){
  return new yyParser;
}
#endif /* Parse_ENGINEALWAYSONSTACK */


/* The following function deletes the "minor type" or semantic value
** associated with a symbol.  The symbol can be either a terminal
** or nonterminal. "yymajor" is the symbol code, and "yypminor" is
** a pointer to the value to be deleted.  The code used to do the 
** deletions is derived from the %destructor and/or %token_destructor
** directives of the input grammar.
*/
static void yy_destructor(
  yyParser *yypParser,    /* The parser */
  YYCODETYPE yymajor,     /* Type code for object to destroy */
  YYMINORTYPE *yypminor   /* The object to be destroyed */
){
  ParseARG_FETCH;
  switch( yymajor ){
    /* Here is inserted the actions which take place when a
    ** terminal or non-terminal is destroyed.  This can happen
    ** when the symbol is popped from the stack during a
    ** reduce or during error processing or when a parser is 
    ** being destroyed before it is finished parsing.
    **
    ** Note: during a reduce, the only symbols destroyed are those
    ** which appear on the RHS of the rule, but which are *not* used
    ** inside the C code.
    */
/********* Begin destructor definitions ***************************************/
      /* TERMINAL Destructor */
    case 1: /* ERROR */
    case 2: /* OR */
    case 3: /* XOR */
    case 4: /* AND */
    case 5: /* NOT */
    case 6: /* NEAR */
    case 7: /* ADJ */
    case 8: /* LOVE */
    case 9: /* HATE */
    case 10: /* HATE_AFTER_AND */
    case 11: /* SYNONYM */
    case 12: /* TERM */
    case 13: /* GROUP_TERM */
    case 14: /* PHR_TERM */
    case 15: /* WILD_TERM */
    case 16: /* PARTIAL_TERM */
    case 17: /* BOOLEAN_FILTER */
    case 18: /* RANGE */
    case 19: /* QUOTE */
    case 20: /* BRA */
    case 21: /* KET */
    case 22: /* UNBROKEN_WORDS */
    case 23: /* EMPTY_GROUP_OK */
{
#line 1805 "queryparser/queryparser.lemony"
 delete (yypminor->yy0); 
#line 1784 "queryparser/queryparser_internal.cc"
}
      break;
    case 26: /* expr */
    case 27: /* prob_expr */
    case 28: /* bool_arg */
    case 30: /* term */
    case 32: /* stop_term */
    case 33: /* compound_term */
{
#line 1880 "queryparser/queryparser.lemony"
 delete (yypminor->yy39); 
#line 1796 "queryparser/queryparser_internal.cc"
}
      break;
    case 29: /* prob */
    case 31: /* stop_prob */
{
#line 1989 "queryparser/queryparser.lemony"
 delete (yypminor->yy40); 
#line 1804 "queryparser/queryparser_internal.cc"
}
      break;
    case 34: /* phrase */
    case 35: /* phrased_term */
    case 37: /* near_expr */
    case 38: /* adj_expr */
{
#line 2180 "queryparser/queryparser.lemony"
 delete (yypminor->yy32); 
#line 1814 "queryparser/queryparser_internal.cc"
}
      break;
    case 36: /* group */
{
#line 2221 "queryparser/queryparser.lemony"
 delete (yypminor->yy14); 
#line 1821 "queryparser/queryparser_internal.cc"
}
      break;
/********* End destructor definitions *****************************************/
    default:  break;   /* If no destructor action specified: do nothing */
  }
  ParseARG_STORE; /* Suppress warning about unused %extra_argument variable */
}

/*
** Pop the parser's stack once.
**
** If there is a destructor routine associated with the token which
** is popped from the stack, then call it.
*/
static void yy_pop_parser_stack(yyParser *pParser){
  Assert( pParser->yystack.size() > 1 );
  yyStackEntry *yytos = &pParser->yystack.back();

  LOGLINE(QUERYPARSER, "Popping " << ParseTokenName(yytos->major));
  yy_destructor(pParser, yytos->major, &yytos->minor);
  pParser->yystack.pop_back();
}

/*
** Clear all secondary memory allocations from the parser
*/
static
void ParseFinalize(yyParser *pParser){
  while( pParser->yystack.size() > 1 ) yy_pop_parser_stack(pParser);
}

#ifndef Parse_ENGINEALWAYSONSTACK
/* 
** Deallocate and destroy a parser.  Destructors are called for
** all stack elements before shutting the parser down.
**
** If the YYPARSEFREENEVERNULL macro exists (for example because it
** is defined in a %include section of the input grammar) then it is
** assumed that the input pointer is never NULL.
*/
static
void ParseFree(
  yyParser *pParser           /* The parser to be deleted */
){
  delete pParser;
}
#endif /* Parse_ENGINEALWAYSONSTACK */

/*
** Return the peak depth of the stack for a parser.
*/
#ifdef YYTRACKMAXSTACKDEPTH
int ParseStackPeak(yyParser *pParser){
  return pParser->yyhwm;
}
#endif

/* This array of booleans keeps track of the parser statement
** coverage.  The element yycoverage[X][Y] is set when the parser
** is in state X and has a lookahead token Y.  In a well-tested
** systems, every element of this matrix should end up being set.
*/
#if defined(YYCOVERAGE)
static unsigned char yycoverage[YYNSTATE][YYNTOKEN];
#endif

/*
** Write into out a description of every state/lookahead combination that
**
**   (1)  has not been used by the parser, and
**   (2)  is not a syntax error.
**
** Return the number of missed state/lookahead combinations.
*/
#if defined(YYCOVERAGE)
int ParseCoverage(FILE *out){
  int stateno, iLookAhead, i;
  int nMissed = 0;
  for(stateno=0; stateno<YYNSTATE; stateno++){
    i = yy_shift_ofst[stateno];
    for(iLookAhead=0; iLookAhead<YYNTOKEN; iLookAhead++){
      if( yy_lookahead[i+iLookAhead]!=iLookAhead ) continue;
      if( yycoverage[stateno][iLookAhead]==0 ) nMissed++;
      if( out ){
        fprintf(out,"State %d lookahead %s %s\n", stateno,
                yyTokenName[iLookAhead],
                yycoverage[stateno][iLookAhead] ? "ok" : "missed");
      }
    }
  }
  return nMissed;
}
#endif

/*
** Find the appropriate action for a parser given the terminal
** look-ahead token iLookAhead.
*/
static unsigned int yy_find_shift_action(
  yyParser *pParser,        /* The parser */
  YYCODETYPE iLookAhead     /* The look-ahead token */
){
  int i;
  int stateno = pParser->yystack.back().stateno;
 
  if( stateno>YY_MAX_SHIFT ) return stateno;
  Assert( stateno <= YY_SHIFT_COUNT );
#if defined(YYCOVERAGE)
  yycoverage[stateno][iLookAhead] = 1;
#endif
  do{
    i = yy_shift_ofst[stateno];
    Assert( i>=0 );
    Assert( i+YYNTOKEN<=(int)(sizeof(yy_lookahead)/sizeof(yy_lookahead[0])) );
    Assert( iLookAhead!=YYNOCODE );
    Assert( iLookAhead < YYNTOKEN );
    i += iLookAhead;
    if( yy_lookahead[i]!=iLookAhead ){
#ifdef YYFALLBACK
      YYCODETYPE iFallback;            /* Fallback token */
      if( iLookAhead<sizeof(yyFallback)/sizeof(yyFallback[0])
             && (iFallback = yyFallback[iLookAhead])!=0 ){
        LOGLINE(QUERYPARSER,
                "FALLBACK " << ParseTokenName(iLookAhead) << " => " <<
                ParseTokenName(iFallback));
        Assert( yyFallback[iFallback]==0 ); /* Fallback loop must terminate */
        iLookAhead = iFallback;
        continue;
      }
#endif
#ifdef YYWILDCARD
      {
        int j = i - iLookAhead + YYWILDCARD;
        if( 
#if YY_SHIFT_MIN+YYWILDCARD<0
          j>=0 &&
#endif
#if YY_SHIFT_MAX+YYWILDCARD>=YY_ACTTAB_COUNT
          j<YY_ACTTAB_COUNT &&
#endif
          yy_lookahead[j]==YYWILDCARD && iLookAhead>0
        ){
          LOGLINE(QUERYPARSER,
                  "WILDCARD " << ParseTokenName(iLookAhead) << " => " <<
                  ParseTokenName(YYWILDCARD));
          return yy_action[j];
        }
      }
#endif /* YYWILDCARD */
      return yy_default[stateno];
    }else{
      return yy_action[i];
    }
  }while(1);
}

/*
** Find the appropriate action for a parser given the non-terminal
** look-ahead token iLookAhead.
*/
static int yy_find_reduce_action(
  int stateno,              /* Current state number */
  YYCODETYPE iLookAhead     /* The look-ahead token */
){
  int i;
#ifdef YYERRORSYMBOL
  if( stateno>YY_REDUCE_COUNT ){
    return yy_default[stateno];
  }
#else
  Assert( stateno<=YY_REDUCE_COUNT );
#endif
  i = yy_reduce_ofst[stateno];
  Assert( iLookAhead!=YYNOCODE );
  i += iLookAhead;
#ifdef YYERRORSYMBOL
  if( i<0 || i>=YY_ACTTAB_COUNT || yy_lookahead[i]!=iLookAhead ){
    return yy_default[stateno];
  }
#else
  Assert( i>=0 && i<YY_ACTTAB_COUNT );
  Assert( yy_lookahead[i]==iLookAhead );
#endif
  return yy_action[i];
}

/*
** The following routine is called if the stack overflows.
** In Xapian this can never happen as we use std::vector to provide a stack
** of indefinite size.
*/
#if 0
static void yyStackOverflow(yyParser *yypParser){
   ParseARG_FETCH;
   yypParser->yyidx--;
#ifndef NDEBUG
   if( yyTraceFILE ){
     fprintf(yyTraceFILE,"%sStack Overflow!\n",yyTracePrompt);
   }
#endif
   while( yypParser->yytos>yypParser->yystack ) yy_pop_parser_stack(yypParser);
   /* Here code is inserted which will execute if the parser
   ** stack ever overflows */
/******** Begin %stack_overflow code ******************************************/
/******** End %stack_overflow code ********************************************/
   ParseARG_STORE; /* Suppress warning about unused %extra_argument var */
}
#endif

/*
** Print tracing information for a SHIFT action
*/
#ifdef XAPIAN_DEBUG_LOG
static void yyTraceShift(yyParser *yypParser, int yyNewState, const char *zTag){
  if( yyNewState<YYNSTATE ){
    LOGLINE(QUERYPARSER, zTag << " '" <<
                         yyTokenName[yypParser->yystack.back().major] <<
                         "', go to state " << yyNewState);
  }else{
    LOGLINE(QUERYPARSER, zTag << " '" <<
                         yyTokenName[yypParser->yystack.back().major] <<
                         "', pending reduce " << yyNewState - YY_MIN_REDUCE);
  }
}
#else
# define yyTraceShift(X,Y,Z)
#endif

/*
** Perform a shift action.
*/
static void yy_shift(
  yyParser *yypParser,          /* The parser to be shifted */
  int yyNewState,               /* The new state to shift in */
  int yyMajor,                  /* The major token to shift in */
  ParseTOKENTYPE yyMinor        /* The minor token to shift in */
){
  if( yyNewState > YY_MAX_SHIFT ){
    yyNewState += YY_MIN_REDUCE - YY_MIN_SHIFTREDUCE;
  }
  yypParser->yystack.push_back(yyStackEntry(yyNewState, yyMajor, yyMinor));
#ifdef YYTRACKMAXSTACKDEPTH
  if( (int)(yypParser->yystack.size()>yypParser->yyhwm ){
    yypParser->yyhwm++;
    Assert( yypParser->yyhwm == (int)(yypParser->yystack.size() );
  }
#endif
  yyTraceShift(yypParser, yyNewState, "Shift");
}

/* The following table contains information about every rule that
** is used during the reduce.
*/
static const struct {
  YYCODETYPE lhs;       /* Symbol on the left-hand side of the rule */
  signed char nrhs;     /* Negative of the number of RHS symbols in the rule */
} yyRuleInfo[] = {
  {   25,   -1 }, /* (0) query ::= expr */
  {   25,    0 }, /* (1) query ::= */
  {   26,   -3 }, /* (2) expr ::= bool_arg AND bool_arg */
  {   26,   -3 }, /* (3) expr ::= bool_arg NOT bool_arg */
  {   26,   -4 }, /* (4) expr ::= bool_arg AND NOT bool_arg */
  {   26,   -4 }, /* (5) expr ::= bool_arg AND HATE_AFTER_AND bool_arg */
  {   26,   -3 }, /* (6) expr ::= bool_arg OR bool_arg */
  {   26,   -3 }, /* (7) expr ::= bool_arg XOR bool_arg */
  {   28,    0 }, /* (8) bool_arg ::= */
  {   27,   -1 }, /* (9) prob_expr ::= prob */
  {   29,   -1 }, /* (10) prob ::= RANGE */
  {   29,   -2 }, /* (11) prob ::= stop_prob RANGE */
  {   29,   -2 }, /* (12) prob ::= stop_term stop_term */
  {   29,   -2 }, /* (13) prob ::= prob stop_term */
  {   29,   -2 }, /* (14) prob ::= LOVE term */
  {   29,   -3 }, /* (15) prob ::= stop_prob LOVE term */
  {   29,   -2 }, /* (16) prob ::= HATE term */
  {   29,   -3 }, /* (17) prob ::= stop_prob HATE term */
  {   29,   -2 }, /* (18) prob ::= HATE BOOLEAN_FILTER */
  {   29,   -3 }, /* (19) prob ::= stop_prob HATE BOOLEAN_FILTER */
  {   29,   -1 }, /* (20) prob ::= BOOLEAN_FILTER */
  {   29,   -2 }, /* (21) prob ::= stop_prob BOOLEAN_FILTER */
  {   29,   -2 }, /* (22) prob ::= LOVE BOOLEAN_FILTER */
  {   29,   -3 }, /* (23) prob ::= stop_prob LOVE BOOLEAN_FILTER */
  {   31,   -1 }, /* (24) stop_prob ::= stop_term */
  {   32,   -1 }, /* (25) stop_term ::= TERM */
  {   30,   -1 }, /* (26) term ::= TERM */
  {   33,   -1 }, /* (27) compound_term ::= WILD_TERM */
  {   33,   -1 }, /* (28) compound_term ::= PARTIAL_TERM */
  {   33,   -3 }, /* (29) compound_term ::= QUOTE phrase QUOTE */
  {   33,   -1 }, /* (30) compound_term ::= phrased_term */
  {   33,   -1 }, /* (31) compound_term ::= group */
  {   33,   -1 }, /* (32) compound_term ::= near_expr */
  {   33,   -1 }, /* (33) compound_term ::= adj_expr */
  {   33,   -3 }, /* (34) compound_term ::= BRA expr KET */
  {   33,   -2 }, /* (35) compound_term ::= SYNONYM TERM */
  {   33,   -1 }, /* (36) compound_term ::= UNBROKEN_WORDS */
  {   34,   -1 }, /* (37) phrase ::= TERM */
  {   34,   -1 }, /* (38) phrase ::= UNBROKEN_WORDS */
  {   34,   -2 }, /* (39) phrase ::= phrase TERM */
  {   34,   -2 }, /* (40) phrase ::= phrase UNBROKEN_WORDS */
  {   35,   -2 }, /* (41) phrased_term ::= TERM PHR_TERM */
  {   35,   -2 }, /* (42) phrased_term ::= phrased_term PHR_TERM */
  {   36,   -2 }, /* (43) group ::= TERM GROUP_TERM */
  {   36,   -2 }, /* (44) group ::= group GROUP_TERM */
  {   36,   -2 }, /* (45) group ::= group EMPTY_GROUP_OK */
  {   37,   -3 }, /* (46) near_expr ::= TERM NEAR TERM */
  {   37,   -3 }, /* (47) near_expr ::= near_expr NEAR TERM */
  {   38,   -3 }, /* (48) adj_expr ::= TERM ADJ TERM */
  {   38,   -3 }, /* (49) adj_expr ::= adj_expr ADJ TERM */
  {   26,   -1 }, /* (50) expr ::= prob_expr */
  {   28,   -1 }, /* (51) bool_arg ::= expr */
  {   27,   -1 }, /* (52) prob_expr ::= term */
  {   31,   -1 }, /* (53) stop_prob ::= prob */
  {   32,   -1 }, /* (54) stop_term ::= compound_term */
  {   30,   -1 }, /* (55) term ::= compound_term */
};

static void yy_accept(yyParser*);  /* Forward Declaration */

/*
** Perform a reduce action and the shift that must immediately
** follow the reduce.
**
** The yyLookahead and yyLookaheadToken parameters provide reduce actions
** access to the lookahead token (if any).  The yyLookahead will be YYNOCODE
** if the lookahead token has already been consumed.  As this procedure is
** only called from one place, optimizing compilers will in-line it, which
** means that the extra parameters have no performance impact.
*/
static void yy_reduce(
  yyParser *yypParser,         /* The parser */
  unsigned int yyruleno,       /* Number of the rule by which to reduce */
  int yyLookahead,             /* Lookahead token, or YYNOCODE if none */
  ParseTOKENTYPE yyLookaheadToken  /* Value of the lookahead token */
){
  int yygoto;                     /* The next state */
  int yyact;                      /* The next action */
  yyStackEntry *yymsp;            /* The top of the parser's stack */
  int yysize;                     /* Amount to pop the stack */
  ParseARG_FETCH;
  (void)yyLookahead;
  (void)yyLookaheadToken;
  yymsp = &yypParser->yystack.back();
  Assert( yyruleno<sizeof(yyRuleInfo)/sizeof(yyRuleInfo[0]) );
#ifdef XAPIAN_DEBUG_LOG
  {
    yysize = yyRuleInfo[yyruleno].nrhs;
    if( yysize ){
      LOGLINE(QUERYPARSER, "Reduce " << yyruleno << " [" <<
                           ParseRuleName(yyruleno) << "], go to state " <<
                           yymsp[yysize].stateno);
    } else {
      LOGLINE(QUERYPARSER, "Reduce " << yyruleno << " [" <<
                           ParseRuleName(yyruleno) << "].");
    }
  }
#endif /* XAPIAN_DEBUG_LOG */
  /*  yygotominor = yyzerominor; */

  /* Check that the stack is large enough to grow by a single entry
  ** if the RHS of the rule is empty.  This ensures that there is room
  ** enough on the stack to push the LHS value without invalidating
  ** pointers into the stack. */
  if( yyRuleInfo[yyruleno].nrhs==0 ){
#if 1
    yypParser->yystack.resize(yypParser->yystack.size() + 1);
    yymsp = &(yypParser->yystack.back()) - 1;
#else
#ifdef YYTRACKMAXSTACKDEPTH
    if( (int)(yypParser->yytos - yypParser->yystack)>yypParser->yyhwm ){
      yypParser->yyhwm++;
      Assert( yypParser->yyhwm == (int)(yypParser->yytos - yypParser->yystack));
    }
#endif
#if YYSTACKDEPTH>0 
    if( yypParser->yytos>=yypParser->yystackEnd ){
      yyStackOverflow(yypParser);
      return;
    }
#else
    if( yypParser->yytos>=&yypParser->yystack[yypParser->yystksz-1] ){
      if( yyGrowStack(yypParser) ){
        yyStackOverflow(yypParser);
        return;
      }
      yymsp = yypParser->yytos;
    }
#endif
#endif
  }

  switch( yyruleno ){
  /* Beginning here are the reduction cases.  A typical example
  ** follows:
  **   case 0:
  **  #line <lineno> <grammarfile>
  **     { ... }           // User supplied code
  **  #line <lineno> <thisfile>
  **     break;
  */
/********** Begin reduce actions **********************************************/
        YYMINORTYPE yylhsminor;
      case 0: /* query ::= expr */
#line 1862 "queryparser/queryparser.lemony"
{
    // Save the parsed query in the State structure so we can return it.
    if (yymsp[0].minor.yy39) {
	state->query = *yymsp[0].minor.yy39;
	delete yymsp[0].minor.yy39;
    } else {
	state->query = Query();
    }
}
#line 2233 "queryparser/queryparser_internal.cc"
        break;
      case 1: /* query ::= */
#line 1872 "queryparser/queryparser.lemony"
{
    // Handle a query string with no terms in.
    state->query = Query();
}
#line 2241 "queryparser/queryparser_internal.cc"
        break;
      case 2: /* expr ::= bool_arg AND bool_arg */
#line 1884 "queryparser/queryparser.lemony"
{
    VET_BOOL_ARGS(yymsp[-2].minor.yy39, yymsp[0].minor.yy39, "AND");
    *yymsp[-2].minor.yy39 &= *yymsp[0].minor.yy39;
    delete yymsp[0].minor.yy39;
}
#line 2250 "queryparser/queryparser_internal.cc"
  yy_destructor(yypParser,4,&yymsp[-1].minor);
        break;
      case 3: /* expr ::= bool_arg NOT bool_arg */
#line 1890 "queryparser/queryparser.lemony"
{
    // 'NOT foo' -> '<alldocuments> NOT foo'
    if (!yymsp[-2].minor.yy39 && (state->flags & QueryParser::FLAG_PURE_NOT)) {
	yymsp[-2].minor.yy39 = new Query("", 1, 0);
    }
    VET_BOOL_ARGS(yymsp[-2].minor.yy39, yymsp[0].minor.yy39, "NOT");
    *yymsp[-2].minor.yy39 &= ~*yymsp[0].minor.yy39;
    delete yymsp[0].minor.yy39;
}
#line 2264 "queryparser/queryparser_internal.cc"
  yy_destructor(yypParser,5,&yymsp[-1].minor);
        break;
      case 4: /* expr ::= bool_arg AND NOT bool_arg */
#line 1900 "queryparser/queryparser.lemony"
{
    VET_BOOL_ARGS(yymsp[-3].minor.yy39, yymsp[0].minor.yy39, "AND NOT");
    *yymsp[-3].minor.yy39 &= ~*yymsp[0].minor.yy39;
    delete yymsp[0].minor.yy39;
}
#line 2274 "queryparser/queryparser_internal.cc"
  yy_destructor(yypParser,4,&yymsp[-2].minor);
  yy_destructor(yypParser,5,&yymsp[-1].minor);
        break;
      case 5: /* expr ::= bool_arg AND HATE_AFTER_AND bool_arg */
#line 1906 "queryparser/queryparser.lemony"
{
    VET_BOOL_ARGS(yymsp[-3].minor.yy39, yymsp[0].minor.yy39, "AND");
    *yymsp[-3].minor.yy39 &= ~*yymsp[0].minor.yy39;
    delete yymsp[0].minor.yy39;
}
#line 2285 "queryparser/queryparser_internal.cc"
  yy_destructor(yypParser,4,&yymsp[-2].minor);
  yy_destructor(yypParser,10,&yymsp[-1].minor);
        break;
      case 6: /* expr ::= bool_arg OR bool_arg */
#line 1912 "queryparser/queryparser.lemony"
{
    VET_BOOL_ARGS(yymsp[-2].minor.yy39, yymsp[0].minor.yy39, "OR");
    *yymsp[-2].minor.yy39 |= *yymsp[0].minor.yy39;
    delete yymsp[0].minor.yy39;
}
#line 2296 "queryparser/queryparser_internal.cc"
  yy_destructor(yypParser,2,&yymsp[-1].minor);
        break;
      case 7: /* expr ::= bool_arg XOR bool_arg */
#line 1918 "queryparser/queryparser.lemony"
{
    VET_BOOL_ARGS(yymsp[-2].minor.yy39, yymsp[0].minor.yy39, "XOR");
    *yymsp[-2].minor.yy39 ^= *yymsp[0].minor.yy39;
    delete yymsp[0].minor.yy39;
}
#line 2306 "queryparser/queryparser_internal.cc"
  yy_destructor(yypParser,3,&yymsp[-1].minor);
        break;
      case 8: /* bool_arg ::= */
#line 1931 "queryparser/queryparser.lemony"
{
    // Set the argument to NULL, which enables the bool_arg-using rules in
    // expr above to report uses of AND, OR, etc which don't have two
    // arguments.
    yymsp[1].minor.yy39 = NULL;
}
#line 2317 "queryparser/queryparser_internal.cc"
        break;
      case 9: /* prob_expr ::= prob */
#line 1943 "queryparser/queryparser.lemony"
{
    yylhsminor.yy39 = yymsp[0].minor.yy40->query;
    yymsp[0].minor.yy40->query = NULL;
    // Handle any "+ terms".
    if (yymsp[0].minor.yy40->love) {
	if (yymsp[0].minor.yy40->love->empty()) {
	    // +<nothing>.
	    delete yylhsminor.yy39;
	    yylhsminor.yy39 = yymsp[0].minor.yy40->love;
	} else if (yylhsminor.yy39) {
	    swap(yylhsminor.yy39, yymsp[0].minor.yy40->love);
	    add_to_query(yylhsminor.yy39, Query::OP_AND_MAYBE, yymsp[0].minor.yy40->love);
	} else {
	    yylhsminor.yy39 = yymsp[0].minor.yy40->love;
	}
	yymsp[0].minor.yy40->love = NULL;
    }
    // Handle any boolean filters.
    if (!yymsp[0].minor.yy40->filter.empty()) {
	if (yylhsminor.yy39) {
	    add_to_query(yylhsminor.yy39, Query::OP_FILTER, yymsp[0].minor.yy40->merge_filters());
	} else {
	    // Make the query a boolean one.
	    yylhsminor.yy39 = new Query(Query::OP_SCALE_WEIGHT, yymsp[0].minor.yy40->merge_filters(), 0.0);
	}
    }
    // Handle any "- terms".
    if (yymsp[0].minor.yy40->hate && !yymsp[0].minor.yy40->hate->empty()) {
	if (!yylhsminor.yy39) {
	    // Can't just hate!
	    yy_parse_failed(yypParser);
	    return;
	}
	*yylhsminor.yy39 = Query(Query::OP_AND_NOT, *yylhsminor.yy39, *yymsp[0].minor.yy40->hate);
    }
    delete yymsp[0].minor.yy40;
}
#line 2358 "queryparser/queryparser_internal.cc"
  yymsp[0].minor.yy39 = yylhsminor.yy39;
        break;
      case 10: /* prob ::= RANGE */
#line 1991 "queryparser/queryparser.lemony"
{
    string grouping = yymsp[0].minor.yy0->name;
    const Query & range = yymsp[0].minor.yy0->as_range_query();
    yymsp[0].minor.yy40 = new ProbQuery; /*P-overwrites-R*/
    yymsp[0].minor.yy40->add_filter_range(grouping, range);
}
#line 2369 "queryparser/queryparser_internal.cc"
        break;
      case 11: /* prob ::= stop_prob RANGE */
#line 1998 "queryparser/queryparser.lemony"
{
    string grouping = yymsp[0].minor.yy0->name;
    const Query & range = yymsp[0].minor.yy0->as_range_query();
    yymsp[-1].minor.yy40->append_filter_range(grouping, range);
}
#line 2378 "queryparser/queryparser_internal.cc"
        break;
      case 12: /* prob ::= stop_term stop_term */
#line 2004 "queryparser/queryparser.lemony"
{
    yymsp[-1].minor.yy40 = new ProbQuery(yymsp[-1].minor.yy39); /*P-overwrites-T*/
    if (yymsp[0].minor.yy39) {
	Query::op op = state->default_op();
	if (yymsp[-1].minor.yy40->query && is_positional(op)) {
	    // If default_op is OP_NEAR or OP_PHRASE, set the window size to
	    // 11 for the first pair of terms and it will automatically grow
	    // by one for each subsequent term.
	    Query * subqs[2] = { yymsp[-1].minor.yy40->query, yymsp[0].minor.yy39 };
	    *(yymsp[-1].minor.yy40->query) = Query(op, subqs, subqs + 2, 11);
	    delete yymsp[0].minor.yy39;
	} else {
	    add_to_query(yymsp[-1].minor.yy40->query, op, yymsp[0].minor.yy39);
	}
    }
}
#line 2398 "queryparser/queryparser_internal.cc"
        break;
      case 13: /* prob ::= prob stop_term */
#line 2021 "queryparser/queryparser.lemony"
{
    // If yymsp[0].minor.yy39 is a stopword, there's nothing to do here.
    if (yymsp[0].minor.yy39) add_to_query(yymsp[-1].minor.yy40->query, state->default_op(), yymsp[0].minor.yy39);
}
#line 2406 "queryparser/queryparser_internal.cc"
        break;
      case 14: /* prob ::= LOVE term */
{  yy_destructor(yypParser,8,&yymsp[-1].minor);
#line 2026 "queryparser/queryparser.lemony"
{
    yymsp[-1].minor.yy40 = new ProbQuery;
    if (state->default_op() == Query::OP_AND) {
	yymsp[-1].minor.yy40->query = yymsp[0].minor.yy39;
    } else {
	yymsp[-1].minor.yy40->love = yymsp[0].minor.yy39;
    }
}
#line 2419 "queryparser/queryparser_internal.cc"
}
        break;
      case 15: /* prob ::= stop_prob LOVE term */
#line 2035 "queryparser/queryparser.lemony"
{
    if (state->default_op() == Query::OP_AND) {
	/* The default op is AND, so we just put loved terms into the query
	 * (in this case the only effect of love is to ignore the stopword
	 * list). */
	add_to_query(yymsp[-2].minor.yy40->query, Query::OP_AND, yymsp[0].minor.yy39);
    } else {
	add_to_query(yymsp[-2].minor.yy40->love, Query::OP_AND, yymsp[0].minor.yy39);
    }
}
#line 2434 "queryparser/queryparser_internal.cc"
  yy_destructor(yypParser,8,&yymsp[-1].minor);
        break;
      case 16: /* prob ::= HATE term */
{  yy_destructor(yypParser,9,&yymsp[-1].minor);
#line 2046 "queryparser/queryparser.lemony"
{
    yymsp[-1].minor.yy40 = new ProbQuery;
    yymsp[-1].minor.yy40->hate = yymsp[0].minor.yy39;
}
#line 2444 "queryparser/queryparser_internal.cc"
}
        break;
      case 17: /* prob ::= stop_prob HATE term */
#line 2051 "queryparser/queryparser.lemony"
{
    add_to_query(yymsp[-2].minor.yy40->hate, Query::OP_OR, yymsp[0].minor.yy39);
}
#line 2452 "queryparser/queryparser_internal.cc"
  yy_destructor(yypParser,9,&yymsp[-1].minor);
        break;
      case 18: /* prob ::= HATE BOOLEAN_FILTER */
{  yy_destructor(yypParser,9,&yymsp[-1].minor);
#line 2055 "queryparser/queryparser.lemony"
{
    yymsp[-1].minor.yy40 = new ProbQuery;
    yymsp[-1].minor.yy40->hate = new Query(yymsp[0].minor.yy0->get_query());
    delete yymsp[0].minor.yy0;
}
#line 2463 "queryparser/queryparser_internal.cc"
}
        break;
      case 19: /* prob ::= stop_prob HATE BOOLEAN_FILTER */
#line 2061 "queryparser/queryparser.lemony"
{
    add_to_query(yymsp[-2].minor.yy40->hate, Query::OP_OR, yymsp[0].minor.yy0->get_query());
    delete yymsp[0].minor.yy0;
}
#line 2472 "queryparser/queryparser_internal.cc"
  yy_destructor(yypParser,9,&yymsp[-1].minor);
        break;
      case 20: /* prob ::= BOOLEAN_FILTER */
#line 2066 "queryparser/queryparser.lemony"
{
    yylhsminor.yy40 = new ProbQuery;
    yylhsminor.yy40->add_filter(yymsp[0].minor.yy0->get_grouping(), yymsp[0].minor.yy0->get_query());
    delete yymsp[0].minor.yy0;
}
#line 2482 "queryparser/queryparser_internal.cc"
  yymsp[0].minor.yy40 = yylhsminor.yy40;
        break;
      case 21: /* prob ::= stop_prob BOOLEAN_FILTER */
#line 2072 "queryparser/queryparser.lemony"
{
    yymsp[-1].minor.yy40->append_filter(yymsp[0].minor.yy0->get_grouping(), yymsp[0].minor.yy0->get_query());
    delete yymsp[0].minor.yy0;
}
#line 2491 "queryparser/queryparser_internal.cc"
        break;
      case 22: /* prob ::= LOVE BOOLEAN_FILTER */
{  yy_destructor(yypParser,8,&yymsp[-1].minor);
#line 2077 "queryparser/queryparser.lemony"
{
    // LOVE BOOLEAN_FILTER(yymsp[0].minor.yy0) is just the same as BOOLEAN_FILTER
    yymsp[-1].minor.yy40 = new ProbQuery;
    yymsp[-1].minor.yy40->filter[yymsp[0].minor.yy0->get_grouping()] = yymsp[0].minor.yy0->get_query();
    delete yymsp[0].minor.yy0;
}
#line 2502 "queryparser/queryparser_internal.cc"
}
        break;
      case 23: /* prob ::= stop_prob LOVE BOOLEAN_FILTER */
#line 2084 "queryparser/queryparser.lemony"
{
    // LOVE BOOLEAN_FILTER(yymsp[0].minor.yy0) is just the same as BOOLEAN_FILTER
    // We OR filters with the same prefix...
    Query & q = yymsp[-2].minor.yy40->filter[yymsp[0].minor.yy0->get_grouping()];
    q |= yymsp[0].minor.yy0->get_query();
    delete yymsp[0].minor.yy0;
}
#line 2514 "queryparser/queryparser_internal.cc"
  yy_destructor(yypParser,8,&yymsp[-1].minor);
        break;
      case 24: /* stop_prob ::= stop_term */
#line 2099 "queryparser/queryparser.lemony"
{
    yymsp[0].minor.yy40 = new ProbQuery(yymsp[0].minor.yy39); /*P-overwrites-T*/
}
#line 2522 "queryparser/queryparser_internal.cc"
        break;
      case 25: /* stop_term ::= TERM */
#line 2112 "queryparser/queryparser.lemony"
{
    if (state->is_stopword(yymsp[0].minor.yy0)) {
	yylhsminor.yy39 = NULL;
	state->add_to_stoplist(yymsp[0].minor.yy0);
    } else {
	yylhsminor.yy39 = new Query(yymsp[0].minor.yy0->get_query_with_auto_synonyms());
    }
    delete yymsp[0].minor.yy0;
}
#line 2535 "queryparser/queryparser_internal.cc"
  yymsp[0].minor.yy39 = yylhsminor.yy39;
        break;
      case 26: /* term ::= TERM */
#line 2129 "queryparser/queryparser.lemony"
{
    yylhsminor.yy39 = new Query(yymsp[0].minor.yy0->get_query_with_auto_synonyms());
    delete yymsp[0].minor.yy0;
}
#line 2544 "queryparser/queryparser_internal.cc"
  yymsp[0].minor.yy39 = yylhsminor.yy39;
        break;
      case 27: /* compound_term ::= WILD_TERM */
#line 2144 "queryparser/queryparser.lemony"
{ yymsp[0].minor.yy39 = yymsp[0].minor.yy0->as_wildcarded_query(state); /*T-overwrites-U*/ }
#line 2550 "queryparser/queryparser_internal.cc"
        break;
      case 28: /* compound_term ::= PARTIAL_TERM */
#line 2147 "queryparser/queryparser.lemony"
{ yymsp[0].minor.yy39 = yymsp[0].minor.yy0->as_partial_query(state); /*T-overwrites-U*/ }
#line 2555 "queryparser/queryparser_internal.cc"
        break;
      case 29: /* compound_term ::= QUOTE phrase QUOTE */
{  yy_destructor(yypParser,19,&yymsp[-2].minor);
#line 2150 "queryparser/queryparser.lemony"
{ yymsp[-2].minor.yy39 = yymsp[-1].minor.yy32->as_phrase_query(); }
#line 2561 "queryparser/queryparser_internal.cc"
  yy_destructor(yypParser,19,&yymsp[0].minor);
}
        break;
      case 30: /* compound_term ::= phrased_term */
#line 2153 "queryparser/queryparser.lemony"
{ yymsp[0].minor.yy39 = yymsp[0].minor.yy32->as_phrase_query(); /*T-overwrites-P*/ }
#line 2568 "queryparser/queryparser_internal.cc"
        break;
      case 31: /* compound_term ::= group */
#line 2156 "queryparser/queryparser.lemony"
{ yymsp[0].minor.yy39 = yymsp[0].minor.yy14->as_group(state); /*T-overwrites-P*/ }
#line 2573 "queryparser/queryparser_internal.cc"
        break;
      case 32: /* compound_term ::= near_expr */
#line 2159 "queryparser/queryparser.lemony"
{ yymsp[0].minor.yy39 = yymsp[0].minor.yy32->as_near_query(); /*T-overwrites-P*/ }
#line 2578 "queryparser/queryparser_internal.cc"
        break;
      case 33: /* compound_term ::= adj_expr */
#line 2162 "queryparser/queryparser.lemony"
{ yymsp[0].minor.yy39 = yymsp[0].minor.yy32->as_adj_query(); /*T-overwrites-P*/ }
#line 2583 "queryparser/queryparser_internal.cc"
        break;
      case 34: /* compound_term ::= BRA expr KET */
{  yy_destructor(yypParser,20,&yymsp[-2].minor);
#line 2165 "queryparser/queryparser.lemony"
{ yymsp[-2].minor.yy39 = yymsp[-1].minor.yy39; }
#line 2589 "queryparser/queryparser_internal.cc"
  yy_destructor(yypParser,21,&yymsp[0].minor);
}
        break;
      case 35: /* compound_term ::= SYNONYM TERM */
{  yy_destructor(yypParser,11,&yymsp[-1].minor);
#line 2167 "queryparser/queryparser.lemony"
{
    yymsp[-1].minor.yy39 = new Query(yymsp[0].minor.yy0->get_query_with_synonyms());
    delete yymsp[0].minor.yy0;
}
#line 2600 "queryparser/queryparser_internal.cc"
}
        break;
      case 36: /* compound_term ::= UNBROKEN_WORDS */
#line 2172 "queryparser/queryparser.lemony"
{
    { yymsp[0].minor.yy39 = yymsp[0].minor.yy0->as_unbroken_query(); /*T-overwrites-U*/ }
}
#line 2608 "queryparser/queryparser_internal.cc"
        break;
      case 37: /* phrase ::= TERM */
#line 2182 "queryparser/queryparser.lemony"
{
    yylhsminor.yy32 = Terms::create(state);
    yylhsminor.yy32->add_positional_term(yymsp[0].minor.yy0);
}
#line 2616 "queryparser/queryparser_internal.cc"
  yymsp[0].minor.yy32 = yylhsminor.yy32;
        break;
      case 38: /* phrase ::= UNBROKEN_WORDS */
#line 2187 "queryparser/queryparser.lemony"
{
    yylhsminor.yy32 = Terms::create(state);
    yymsp[0].minor.yy0->as_positional_unbroken(yylhsminor.yy32);
}
#line 2625 "queryparser/queryparser_internal.cc"
  yymsp[0].minor.yy32 = yylhsminor.yy32;
        break;
      case 39: /* phrase ::= phrase TERM */
      case 42: /* phrased_term ::= phrased_term PHR_TERM */ yytestcase(yyruleno==42);
#line 2192 "queryparser/queryparser.lemony"
{
    yymsp[-1].minor.yy32->add_positional_term(yymsp[0].minor.yy0);
}
#line 2634 "queryparser/queryparser_internal.cc"
        break;
      case 40: /* phrase ::= phrase UNBROKEN_WORDS */
#line 2196 "queryparser/queryparser.lemony"
{
    yymsp[0].minor.yy0->as_positional_unbroken(yymsp[-1].minor.yy32);
}
#line 2641 "queryparser/queryparser_internal.cc"
        break;
      case 41: /* phrased_term ::= TERM PHR_TERM */
#line 2207 "queryparser/queryparser.lemony"
{
    yylhsminor.yy32 = Terms::create(state);
    yylhsminor.yy32->add_positional_term(yymsp[-1].minor.yy0);
    yylhsminor.yy32->add_positional_term(yymsp[0].minor.yy0);
}
#line 2650 "queryparser/queryparser_internal.cc"
  yymsp[-1].minor.yy32 = yylhsminor.yy32;
        break;
      case 43: /* group ::= TERM GROUP_TERM */
#line 2223 "queryparser/queryparser.lemony"
{
    yymsp[-1].minor.yy14 = TermGroup::create(yymsp[-1].minor.yy0, yymsp[0].minor.yy0); /*P-overwrites-T*/
}
#line 2658 "queryparser/queryparser_internal.cc"
        break;
      case 44: /* group ::= group GROUP_TERM */
#line 2227 "queryparser/queryparser.lemony"
{
    yymsp[-1].minor.yy14->add_term(yymsp[0].minor.yy0);
}
#line 2665 "queryparser/queryparser_internal.cc"
        break;
      case 45: /* group ::= group EMPTY_GROUP_OK */
#line 2231 "queryparser/queryparser.lemony"
{
    yymsp[-1].minor.yy14->set_empty_ok();
}
#line 2672 "queryparser/queryparser_internal.cc"
  yy_destructor(yypParser,23,&yymsp[0].minor);
        break;
      case 46: /* near_expr ::= TERM NEAR TERM */
      case 48: /* adj_expr ::= TERM ADJ TERM */ yytestcase(yyruleno==48);
#line 2241 "queryparser/queryparser.lemony"
{
    yylhsminor.yy32 = Terms::create(state);
    yylhsminor.yy32->add_positional_term(yymsp[-2].minor.yy0);
    yylhsminor.yy32->add_positional_term(yymsp[0].minor.yy0);
    if (yymsp[-1].minor.yy0) {
	yylhsminor.yy32->adjust_window(yymsp[-1].minor.yy0->get_termpos());
	delete yymsp[-1].minor.yy0;
    }
}
#line 2687 "queryparser/queryparser_internal.cc"
  yymsp[-2].minor.yy32 = yylhsminor.yy32;
        break;
      case 47: /* near_expr ::= near_expr NEAR TERM */
      case 49: /* adj_expr ::= adj_expr ADJ TERM */ yytestcase(yyruleno==49);
#line 2251 "queryparser/queryparser.lemony"
{
    yymsp[-2].minor.yy32->add_positional_term(yymsp[0].minor.yy0);
    if (yymsp[-1].minor.yy0) {
	yymsp[-2].minor.yy32->adjust_window(yymsp[-1].minor.yy0->get_termpos());
	delete yymsp[-1].minor.yy0;
    }
}
#line 2700 "queryparser/queryparser_internal.cc"
        break;
      default:
      /* (50) expr ::= prob_expr (OPTIMIZED OUT) */ Assert(yyruleno!=50);
      /* (51) bool_arg ::= expr */ yytestcase(yyruleno==51);
      /* (52) prob_expr ::= term (OPTIMIZED OUT) */ Assert(yyruleno!=52);
      /* (53) stop_prob ::= prob */ yytestcase(yyruleno==53);
      /* (54) stop_term ::= compound_term */ yytestcase(yyruleno==54);
      /* (55) term ::= compound_term */ yytestcase(yyruleno==55);
        break;
/********** End reduce actions ************************************************/
  }
  Assert( yyruleno<sizeof(yyRuleInfo)/sizeof(yyRuleInfo[0]) );
  yygoto = yyRuleInfo[yyruleno].lhs;
  yysize = yyRuleInfo[yyruleno].nrhs;
  yyact = yy_find_reduce_action(yymsp[yysize].stateno,static_cast<YYCODETYPE>(yygoto));

  /* There are no SHIFTREDUCE actions on nonterminals because the table
  ** generator has simplified them to pure REDUCE actions. */
  Assert( !(yyact>YY_MAX_SHIFT && yyact<=YY_MAX_SHIFTREDUCE) );

  /* It is not possible for a REDUCE to be followed by an error */
  Assert( yyact!=YY_ERROR_ACTION );

  yymsp += yysize+1;
  if (yysize) {
    yypParser->yystack.resize(yypParser->yystack.size() + yysize+1);
  }
  yymsp->stateno = static_cast<YYACTIONTYPE>(yyact);
  yymsp->major = static_cast<YYCODETYPE>(yygoto);
  yyTraceShift(yypParser, yyact, "... then shift");
}

/*
** The following code executes when the parse fails
*/
#ifndef YYNOERRORRECOVERY
static void yy_parse_failed(
  yyParser *yypParser           /* The parser */
){
  ParseARG_FETCH;
  LOGLINE(QUERYPARSER, "Fail!");
  while( yypParser->yystack.size() > 1 ) yy_pop_parser_stack(yypParser);
  /* Here code is inserted which will be executed whenever the
  ** parser fails */
/************ Begin %parse_failure code ***************************************/
#line 1809 "queryparser/queryparser.lemony"

    // If we've not already set an error message, set a default one.
    if (!state->error) state->error = "parse error";
#line 2750 "queryparser/queryparser_internal.cc"
/************ End %parse_failure code *****************************************/
  ParseARG_STORE; /* Suppress warning about unused %extra_argument variable */
}
#endif /* YYNOERRORRECOVERY */

/*
** The following code executes when a syntax error first occurs.
*/
static void yy_syntax_error(
  yyParser *yypParser,           /* The parser */
  int yymajor,                   /* The major type of the error token */
  ParseTOKENTYPE yyminor         /* The minor type of the error token */
){
  ParseARG_FETCH;
  (void)yymajor;
  (void)yyminor;
#define TOKEN yyminor
/************ Begin %syntax_error code ****************************************/
#line 1814 "queryparser/queryparser.lemony"

    yy_parse_failed(yypParser);
#line 2772 "queryparser/queryparser_internal.cc"
/************ End %syntax_error code ******************************************/
  ParseARG_STORE; /* Suppress warning about unused %extra_argument variable */
}

/*
** The following is executed when the parser accepts
*/
static void yy_accept(
  yyParser *yypParser           /* The parser */
){
  ParseARG_FETCH;
  LOGLINE(QUERYPARSER, "Accept!");
#ifndef YYNOERRORRECOVERY
  yypParser->yyerrcnt = -1;
#endif
  AssertEq( yypParser->yystack.size(), 1 );
  /* Here code is inserted which will be executed whenever the
  ** parser accepts */
/*********** Begin %parse_accept code *****************************************/
/*********** End %parse_accept code *******************************************/
  ParseARG_STORE; /* Suppress warning about unused %extra_argument variable */
}

/* The main parser program.
** The first argument is a pointer to a structure obtained from
** "ParseAlloc" which describes the current state of the parser.
** The second argument is the major token number.  The third is
** the minor token.  The fourth optional argument is whatever the
** user wants (and specified in the grammar) and is available for
** use by the action routines.
**
** Inputs:
** <ul>
** <li> A pointer to the parser (an opaque structure.)
** <li> The major token number.
** <li> The minor token number.
** <li> An option argument of a grammar-specified type.
** </ul>
**
** Outputs:
** None.
*/
static
void Parse(
  yyParser *yypParser,         /* The parser */
  int yymajor,                 /* The major token code number */
  ParseTOKENTYPE yyminor       /* The value for the token */
  ParseARG_PDECL               /* Optional %extra_argument parameter */
){
  YYMINORTYPE yyminorunion;
  unsigned int yyact;   /* The parser action. */
#if !defined(YYERRORSYMBOL) && !defined(YYNOERRORRECOVERY)
  int yyendofinput;     /* True if we are at the end of input */
#endif
#ifdef YYERRORSYMBOL
  int yyerrorhit = 0;   /* True if yymajor has invoked an error */
#endif

#if !defined(YYERRORSYMBOL) && !defined(YYNOERRORRECOVERY)
  yyendofinput = (yymajor==0);
#endif
  ParseARG_STORE;

#ifdef XAPIAN_DEBUG_LOG
  {
    int stateno = yypParser->yystack.back().stateno;
    if( stateno < YY_MIN_REDUCE ){
      LOGLINE(QUERYPARSER, "Input '" << ParseTokenName(yymajor) <<
                           "'," << (yyminor ? yyminor->name : "<<null>>") <<
                           "in state " << stateno);
    }else{
      LOGLINE(QUERYPARSER, "Input '" << ParseTokenName(yymajor) <<
                           "'," << (yyminor ? yyminor->name : "<<null>>") <<
                           "with pending reduce " << stateno-YY_MIN_REDUCE);
    }
  }
#endif

  do{
    yyact = yy_find_shift_action(yypParser,static_cast<YYCODETYPE>(yymajor));
    if( yyact >= YY_MIN_REDUCE ){
      yy_reduce(yypParser,yyact-YY_MIN_REDUCE,yymajor,yyminor);
    }else if( yyact <= YY_MAX_SHIFTREDUCE ){
      yy_shift(yypParser,yyact,yymajor,yyminor);
#ifndef YYNOERRORRECOVERY
      yypParser->yyerrcnt--;
#endif
      yymajor = YYNOCODE;
    }else if( yyact==YY_ACCEPT_ACTION ){
      yypParser->yystack.pop_back();
      yy_accept(yypParser);
      return;
    }else{
      Assert( yyact == YY_ERROR_ACTION );
      yyminorunion.yy0 = yyminor;
#ifdef YYERRORSYMBOL
      int yymx;
#endif
      LOGLINE(QUERYPARSER, "Syntax Error!");
#ifdef YYERRORSYMBOL
      /* A syntax error has occurred.
      ** The response to an error depends upon whether or not the
      ** grammar defines an error token "ERROR".  
      **
      ** This is what we do if the grammar does define ERROR:
      **
      **  * Call the %syntax_error function.
      **
      **  * Begin popping the stack until we enter a state where
      **    it is legal to shift the error symbol, then shift
      **    the error symbol.
      **
      **  * Set the error count to three.
      **
      **  * Begin accepting and shifting new tokens.  No new error
      **    processing will occur until three tokens have been
      **    shifted successfully.
      **
      */
      if( yypParser->yyerrcnt<0 ){
        yy_syntax_error(yypParser,yymajor,yyminor);
      }
      yymx = yypParser->yystack.back().major;
      if( yymx==YYERRORSYMBOL || yyerrorhit ){
        LOGLINE(QUERYPARSER, "Discard input token " << ParseTokenName(yymajor));
        yy_destructor(yypParser, static_cast<YYCODETYPE>(yymajor), &yyminorunion);
        yymajor = YYNOCODE;
      }else{
        while( !yypParser->yystack.empty()
            && yymx != YYERRORSYMBOL
            && (yyact = yy_find_reduce_action(
                        yypParser->yystack.back().stateno,
                        YYERRORSYMBOL)) >= YY_MIN_REDUCE
        ){
          yy_pop_parser_stack(yypParser);
        }
        if( yypParser->yystack.empty() || yymajor==0 ){
          yy_destructor(yypParser,static_cast<YYCODETYPE>(yymajor),&yyminorunion);
          yy_parse_failed(yypParser);
#ifndef YYNOERRORRECOVERY
          yypParser->yyerrcnt = -1;
#endif
          yymajor = YYNOCODE;
        }else if( yymx!=YYERRORSYMBOL ){
          yy_shift(yypParser,yyact,YYERRORSYMBOL,yyminor);
        }
      }
      yypParser->yyerrcnt = 3;
      yyerrorhit = 1;
#elif defined(YYNOERRORRECOVERY)
      /* If the YYNOERRORRECOVERY macro is defined, then do not attempt to
      ** do any kind of error recovery.  Instead, simply invoke the syntax
      ** error routine and continue going as if nothing had happened.
      **
      ** Applications can set this macro (for example inside %include) if
      ** they intend to abandon the parse upon the first syntax error seen.
      */
      yy_syntax_error(yypParser,yymajor, yyminor);
      yy_destructor(yypParser,static_cast<YYCODETYPE>(yymajor),&yyminorunion);
      yymajor = YYNOCODE;
      
#else  /* YYERRORSYMBOL is not defined */
      /* This is what we do if the grammar does not define ERROR:
      **
      **  * Report an error message, and throw away the input token.
      **
      **  * If the input token is $, then fail the parse.
      **
      ** As before, subsequent error messages are suppressed until
      ** three input tokens have been successfully shifted.
      */
      if( yypParser->yyerrcnt<=0 ){
        yy_syntax_error(yypParser,yymajor, yyminor);
      }
      yypParser->yyerrcnt = 3;
      yy_destructor(yypParser,static_cast<YYCODETYPE>(yymajor),&yyminorunion);
      if( yyendofinput ){
        yy_parse_failed(yypParser);
#ifndef YYNOERRORRECOVERY
        yypParser->yyerrcnt = -1;
#endif
      }
      yymajor = YYNOCODE;
#endif
    }
  }while( yymajor!=YYNOCODE && yypParser->yystack.size() > 1 );
#ifdef XAPIAN_DEBUG_LOG
  {
    int i;
    LOGLINE(QUERYPARSER, "Return. Stack=");
    for(i=1; i<=(int)yypParser->yystack.size(); i++)
      LOGLINE(QUERYPARSER, yyTokenName[yypParser->yystack[i].major]);
  }
#endif
  return;
}

// Select C++ syntax highlighting in vim editor: vim: syntax=cpp
#line 799 "queryparser/queryparser.lemony"


Query
QueryParser::Internal::parse_query(const string &qs, unsigned flags,
				   const string &default_prefix)
{
    bool try_word_break = (flags & FLAG_NGRAMS) || is_ngram_enabled();

    // Set ranges if we may have to handle ranges in the query.
    bool ranges = !rangeprocs.empty() && (qs.find("..") != string::npos);

    termpos term_pos = 1;
    Utf8Iterator it(qs), end;

    State state(this, flags);

    // To successfully apply more than one spelling correction to a query
    // string, we must keep track of the offset due to previous corrections.
    int correction_offset = 0;
    corrected_query.resize(0);

    // Stack of prefixes, used for phrases and subexpressions.
    list<const FieldInfo *> prefix_stack;

    // If default_prefix is specified, use it.  Otherwise, use any list
    // that has been set for the empty prefix.
    const FieldInfo def_pfx(NON_BOOLEAN, default_prefix);
    {
	const FieldInfo * default_field_info = &def_pfx;
	if (default_prefix.empty()) {
	    auto f = field_map.find(string());
	    if (f != field_map.end()) default_field_info = &(f->second);
	}

	// We always have the current prefix on the top of the stack.
	prefix_stack.push_back(default_field_info);
    }

    yyParser parser;

    unsigned newprev = ' ';
main_lex_loop:
    enum {
	DEFAULT, IN_QUOTES, IN_PREFIXED_QUOTES, IN_PHRASED_TERM, IN_GROUP,
	IN_GROUP2, EXPLICIT_SYNONYM
    } mode = DEFAULT;
    while (it != end && !state.error) {
	bool last_was_operator = false;
	bool last_was_operator_needing_term = false;
	if (mode == EXPLICIT_SYNONYM) mode = DEFAULT;
	if (false) {
just_had_operator:
	    if (it == end) break;
	    mode = DEFAULT;
	    last_was_operator_needing_term = false;
	    last_was_operator = true;
	}
	if (false) {
just_had_operator_needing_term:
	    last_was_operator_needing_term = true;
	    last_was_operator = true;
	}
	if (mode == IN_PHRASED_TERM) mode = DEFAULT;
	if (is_whitespace(*it)) {
	    newprev = ' ';
	    ++it;
	    it = find_if(it, end, is_not_whitespace);
	    if (it == end) break;
	}

	if (ranges &&
	    (mode == DEFAULT || mode == IN_GROUP || mode == IN_GROUP2)) {
	    // Scan forward to see if this could be the "start of range"
	    // token.  Sadly this has O(n²) tendencies, though at least
	    // "n" is the number of words in a query which is likely to
	    // remain fairly small.  FIXME: can we tokenise more elegantly?
	    Utf8Iterator it_initial = it;
	    Utf8Iterator p = it;
	    unsigned ch = 0;
	    while (p != end) {
		if (ch == '.' && *p == '.') {
		    string a;
		    while (it != p) {
			Unicode::append_utf8(a, *it++);
		    }
		    // Trim off the trailing ".".
		    a.resize(a.size() - 1);
		    ++p;
		    // Either end of the range can be empty (for an open-ended
		    // range) but both can't be empty.
		    if (!a.empty() || (p != end && *p > ' ' && *p != ')')) {
			string b;
			// Allow any character except whitespace and ')' in the
			// upper bound.
			while (p != end && *p > ' ' && *p != ')') {
			    Unicode::append_utf8(b, *p++);
			}
			Term * range = state.range(a, b);
			if (!range) {
			    state.error = "Unknown range operation";
			    if (a.find(':', 1) == string::npos) {
				goto done;
			    }
			    // Might be a boolean filter with ".." in.  Leave
			    // state.error in case it isn't.
			    it = it_initial;
			    break;
			}
			Parse(&parser, RANGE, range, &state);
		    }
		    it = p;
		    goto main_lex_loop;
		}
		ch = *p;
		// Allow any character except whitespace and '(' in the lower
		// bound.
		if (ch <= ' ' || ch == '(') break;
		++p;
	    }
	}

	if (!is_wordchar(*it)) {
	    unsigned prev = newprev;
	    unsigned ch = *it++;
	    newprev = ch;
	    // Drop out of IN_GROUP mode.
	    if (mode == IN_GROUP || mode == IN_GROUP2)
		mode = DEFAULT;
	    switch (ch) {
	      case '"':
	      case 0x201c: // Left curly double quote.
	      case 0x201d: // Right curly double quote.
		// Quoted phrase.
		if (mode == DEFAULT) {
		    // Skip whitespace.
		    it = find_if(it, end, is_not_whitespace);
		    if (it == end) {
			// Ignore an unmatched " at the end of the query to
			// avoid generating an empty pair of QUOTEs which will
			// cause a parse error.
			goto done;
		    }
		    if (is_double_quote(*it)) {
			// Ignore empty "" (but only if we're not already
			// IN_QUOTES as we don't merge two adjacent quoted
			// phrases!)
			newprev = *it++;
			break;
		    }
		}
		if (flags & QueryParser::FLAG_PHRASE) {
		    if (ch == '"' && it != end && *it == '"') {
			++it;
			// Handle "" inside a quoted phrase as an escaped " for
			// consistency with quoted boolean terms.
			break;
		    }
		    Parse(&parser, QUOTE, NULL, &state);
		    if (mode == DEFAULT) {
			mode = IN_QUOTES;
		    } else {
			// Remove the prefix we pushed for this phrase.
			if (mode == IN_PREFIXED_QUOTES)
			    prefix_stack.pop_back();
			mode = DEFAULT;
		    }
		}
		break;

	      case '+': case '-': // Loved or hated term/phrase/subexpression.
		// Ignore + or - at the end of the query string.
		if (it == end) goto done;
		if (prev > ' ' && prev != '(') {
		    // Or if not after whitespace or an open bracket.
		    break;
		}
		if (is_whitespace(*it) || *it == '+' || *it == '-') {
		    // Ignore + or - followed by a space, or further + or -.
		    // Postfix + (such as in C++ and H+) is handled as part of
		    // the term lexing code in parse_term().
		    newprev = *it++;
		    break;
		}
		if (mode == DEFAULT && (flags & FLAG_LOVEHATE)) {
		    int token;
		    if (ch == '+') {
			token = LOVE;
		    } else if (last_was_operator) {
			token = HATE_AFTER_AND;
		    } else {
			token = HATE;
		    }
		    Parse(&parser, token, NULL, &state);
		    goto just_had_operator_needing_term;
		}
		// Need to prevent the term after a LOVE or HATE starting a
		// term group...
		break;

	      case '(': // Bracketed subexpression.
		// Skip whitespace.
		it = find_if(it, end, is_not_whitespace);
		// Ignore ( at the end of the query string.
		if (it == end) goto done;
		if (prev > ' ' && strchr("()+-", prev) == NULL) {
		    // Or if not after whitespace or a bracket or '+' or '-'.
		    break;
		}
		if (*it == ')') {
		    // Ignore empty ().
		    newprev = *it++;
		    break;
		}
		if (mode == DEFAULT && (flags & FLAG_BOOLEAN)) {
		    prefix_stack.push_back(prefix_stack.back());
		    Parse(&parser, BRA, NULL, &state);
		}
		break;

	      case ')': // End of bracketed subexpression.
		if (mode == DEFAULT && (flags & FLAG_BOOLEAN)) {
		    // Remove the prefix we pushed for the corresponding BRA.
		    // If brackets are unmatched, it's a syntax error, but
		    // that's no excuse to SEGV!
		    if (prefix_stack.size() > 1) prefix_stack.pop_back();
		    Parse(&parser, KET, NULL, &state);
		}
		break;

	      case '~': // Synonym expansion.
		// Ignore at the end of the query string.
		if (it == end) goto done;
		if (mode == DEFAULT && (flags & FLAG_SYNONYM)) {
		    if (prev > ' ' && strchr("+-(", prev) == NULL) {
			// Or if not after whitespace, +, -, or an open bracket.
			break;
		    }
		    if (!is_wordchar(*it)) {
			// Ignore if not followed by a word character.
			break;
		    }
		    Parse(&parser, SYNONYM, NULL, &state);
		    mode = EXPLICIT_SYNONYM;
		    goto just_had_operator_needing_term;
		}
		break;
	    }
	    // Skip any other characters.
	    continue;
	}

	Assert(is_wordchar(*it));

	size_t term_start_index = it.raw() - qs.data();

	newprev = 'A'; // Any letter will do...

	// A term, a prefix, or a boolean operator.
	const FieldInfo * field_info = NULL;
	if ((mode == DEFAULT || mode == IN_GROUP || mode == IN_GROUP2 || mode == EXPLICIT_SYNONYM) &&
	    !field_map.empty()) {
	    // Check for a fieldname prefix (e.g. title:historical).
	    Utf8Iterator p = find_if(it, end, is_not_wordchar);
	    if (p != end && *p == ':' && ++p != end && *p > ' ' && *p != ')') {
		string field;
		p = it;
		while (*p != ':')
		    Unicode::append_utf8(field, *p++);
		map<string, FieldInfo>::const_iterator f;
		f = field_map.find(field);
		if (f != field_map.end()) {
		    // Special handling for prefixed fields, depending on the
		    // type of the prefix.
		    unsigned ch = *++p;
		    field_info = &(f->second);

		    if (field_info->type != NON_BOOLEAN) {
			// Drop out of IN_GROUP if we're in it.
			if (mode == IN_GROUP || mode == IN_GROUP2)
			    mode = DEFAULT;
			it = p;
			string name;
			if (it != end && is_double_quote(*it)) {
			    // Quoted boolean term (can contain any character).
			    bool fancy = (*it != '"');
			    ++it;
			    while (it != end) {
				if (*it == '"') {
				    // Interpret "" as an escaped ".
				    if (++it == end || *it != '"')
					break;
				} else if (fancy && is_double_quote(*it)) {
				    // If the opening quote was ASCII, then the
				    // closing one must be too - otherwise
				    // the user can't protect non-ASCII double
				    // quote characters by quoting or escaping.
				    ++it;
				    break;
				}
				Unicode::append_utf8(name, *it++);
			    }
			} else {
			    // Can't boolean filter prefix a subexpression, so
			    // just use anything following the prefix until the
			    // next space or ')' as part of the boolean filter
			    // term.
			    while (it != end && *it > ' ' && *it != ')')
				Unicode::append_utf8(name, *it++);
			}
			// Build the unstemmed form in field.
			field += ':';
			field += name;
			// Clear any pending range error.
			state.error = NULL;
			Term * token = new Term(&state, name, field_info, field);
			Parse(&parser, BOOLEAN_FILTER, token, &state);
			continue;
		    }

		    if ((flags & FLAG_PHRASE) && is_double_quote(ch)) {
			// Prefixed phrase, e.g.: subject:"space flight"
			mode = IN_PREFIXED_QUOTES;
			Parse(&parser, QUOTE, NULL, &state);
			it = p;
			newprev = ch;
			++it;
			prefix_stack.push_back(field_info);
			continue;
		    }

		    if (ch == '(' && (flags & FLAG_BOOLEAN)) {
			// Prefixed subexpression, e.g.: title:(fast NEAR food)
			mode = DEFAULT;
			Parse(&parser, BRA, NULL, &state);
			it = p;
			newprev = ch;
			++it;
			prefix_stack.push_back(field_info);
			continue;
		    }

		    if (ch != ':') {
			// Allow 'path:/usr/local' but not 'foo::bar::baz'.
			while (is_phrase_generator(ch)) {
			    if (++p == end)
				goto not_prefix;
			    ch = *p;
			}
		    }

		    if (is_wordchar(ch)) {
			// Prefixed term.
			it = p;
		    } else {
not_prefix:
			// It looks like a prefix but isn't, so parse it as
			// text instead.
			field_info = NULL;
		    }
		}
	    }
	}

phrased_term:
	bool was_acronym;
	bool needs_word_break = false;
	string term = parse_term(it, end, try_word_break,
                                 needs_word_break, was_acronym);

	if ((mode == DEFAULT || mode == IN_GROUP || mode == IN_GROUP2) &&
	    (flags & FLAG_BOOLEAN) &&
	    // Don't want to interpret A.N.D. as an AND operator.
	    !was_acronym &&
	    !field_info &&
	    term.size() >= 2 && term.size() <= 4 && U_isalpha(term[0])) {
	    // Boolean operators.
	    string op = term;
	    if (flags & FLAG_BOOLEAN_ANY_CASE) {
		for (string::iterator i = op.begin(); i != op.end(); ++i) {
		    *i = C_toupper(*i);
		}
	    }
	    if (op.size() == 3) {
		if (op == "AND") {
		    Parse(&parser, AND, NULL, &state);
		    goto just_had_operator;
		}
		if (op == "NOT") {
		    Parse(&parser, NOT, NULL, &state);
		    goto just_had_operator;
		}
		if (op == "XOR") {
		    Parse(&parser, XOR, NULL, &state);
		    goto just_had_operator;
		}
		if (op == "ADJ") {
		    if (it != end && *it == '/') {
			size_t width = 0;
			Utf8Iterator p = it;
			while (++p != end && U_isdigit(*p)) {
			    width = (width * 10) + (*p - '0');
			}
			if (width && (p == end || is_whitespace(*p))) {
			    it = p;
			    Parse(&parser, ADJ, new Term(width), &state);
			    goto just_had_operator;
			}
		    } else {
			Parse(&parser, ADJ, NULL, &state);
			goto just_had_operator;
		    }
		}
	    } else if (op.size() == 2) {
		if (op == "OR") {
		    Parse(&parser, OR, NULL, &state);
		    goto just_had_operator;
		}
	    } else if (op.size() == 4) {
		if (op == "NEAR") {
		    if (it != end && *it == '/') {
			size_t width = 0;
			Utf8Iterator p = it;
			while (++p != end && U_isdigit(*p)) {
			    width = (width * 10) + (*p - '0');
			}
			if (width && (p == end || is_whitespace(*p))) {
			    it = p;
			    Parse(&parser, NEAR, new Term(width), &state);
			    goto just_had_operator;
			}
		    } else {
			Parse(&parser, NEAR, NULL, &state);
			goto just_had_operator;
		    }
		}
	    }
	}

	// If no prefix is set, use the default one.
	if (!field_info) field_info = prefix_stack.back();

	Assert(field_info->type == NON_BOOLEAN);

	{
	    string unstemmed_term(term);
	    term = Unicode::tolower(term);

	    // Reuse stem_strategy - STEM_SOME here means "stem terms except
	    // when used with positional operators".
	    stem_strategy stem_term = stem_action;
	    if (stem_term != STEM_NONE) {
		if (stemmer.is_none()) {
		    stem_term = STEM_NONE;
		} else if (stem_term == STEM_SOME ||
			   stem_term == STEM_SOME_FULL_POS) {
		    if (!should_stem(unstemmed_term) ||
			(it != end && is_stem_preventer(*it))) {
			// Don't stem this particular term.
			stem_term = STEM_NONE;
		    }
		}
	    }

	    Term * term_obj = new Term(&state, term, field_info,
				       unstemmed_term, stem_term, term_pos++);

	    if (needs_word_break) {
		Parse(&parser, UNBROKEN_WORDS, term_obj, &state);
		// Drop out of IN_GROUP mode.
		if (mode == IN_GROUP || mode == IN_GROUP2)
		    mode = DEFAULT;
		if (it == end) break;
		continue;
	    }

	    if (mode == DEFAULT || mode == IN_GROUP || mode == IN_GROUP2) {
		if (it != end) {
		    if ((flags & FLAG_WILDCARD) && *it == '*') {
			Utf8Iterator p(it);
			++p;
			if (p == end || !is_wordchar(*p)) {
			    it = p;
			    if (mode == IN_GROUP || mode == IN_GROUP2) {
				// Drop out of IN_GROUP and flag that the group
				// can be empty if all members are stopwords.
				if (mode == IN_GROUP2)
				    Parse(&parser, EMPTY_GROUP_OK, NULL, &state);
				mode = DEFAULT;
			    }
			    // Wildcard at end of term (also known as
			    // "right truncation").
			    Parse(&parser, WILD_TERM, term_obj, &state);
			    continue;
			}
		    }
		} else {
		    if (flags & FLAG_PARTIAL) {
			if (mode == IN_GROUP || mode == IN_GROUP2) {
			    // Drop out of IN_GROUP and flag that the group
			    // can be empty if all members are stopwords.
			    if (mode == IN_GROUP2)
				Parse(&parser, EMPTY_GROUP_OK, NULL, &state);
			    mode = DEFAULT;
			}
			// Final term of a partial match query, with no
			// following characters - treat as a wildcard.
			Parse(&parser, PARTIAL_TERM, term_obj, &state);
			continue;
		    }
		}
	    }

	    // Check spelling, if we're a normal term, and any of the prefixes
	    // are empty.
	    if ((flags & FLAG_SPELLING_CORRECTION) && !was_acronym) {
		const auto& prefixes = field_info->prefixes;
		for (const string& prefix : prefixes) {
		    if (!prefix.empty())
			continue;
		    const string & suggest = db.get_spelling_suggestion(term);
		    if (!suggest.empty()) {
			if (corrected_query.empty()) corrected_query = qs;
			size_t term_end_index = it.raw() - qs.data();
			size_t n = term_end_index - term_start_index;
			size_t pos = term_start_index + correction_offset;
			corrected_query.replace(pos, n, suggest);
			correction_offset += suggest.size();
			correction_offset -= n;
		    }
		    break;
		}
	    }

	    if (mode == IN_PHRASED_TERM) {
		Parse(&parser, PHR_TERM, term_obj, &state);
	    } else {
		// See if the next token will be PHR_TERM - if so, this one
		// needs to be TERM not GROUP_TERM.
		if ((mode == IN_GROUP || mode == IN_GROUP2) &&
		    is_phrase_generator(*it)) {
		    // FIXME: can we clean this up?
		    Utf8Iterator p = it;
		    do {
			++p;
		    } while (p != end && is_phrase_generator(*p));
		    // Don't generate a phrase unless the phrase generators are
		    // immediately followed by another term.
		    if (p != end && is_wordchar(*p)) {
			mode = DEFAULT;
		    }
		}

		int token = TERM;
		if (mode == IN_GROUP || mode == IN_GROUP2) {
		    mode = IN_GROUP2;
		    token = GROUP_TERM;
		}
		Parse(&parser, token, term_obj, &state);
		if (token == TERM && mode != DEFAULT)
		    continue;
	    }
	}

	if (it == end) break;

	if (is_phrase_generator(*it)) {
	    // Skip multiple phrase generators.
	    do {
		++it;
	    } while (it != end && is_phrase_generator(*it));
	    // Don't generate a phrase unless the phrase generators are
	    // immediately followed by another term.
	    if (it != end && is_wordchar(*it)) {
		mode = IN_PHRASED_TERM;
		term_start_index = it.raw() - qs.data();
		goto phrased_term;
	    }
	} else if (mode == DEFAULT || mode == IN_GROUP || mode == IN_GROUP2) {
	    int old_mode = mode;
	    mode = DEFAULT;
	    if (!last_was_operator_needing_term && is_whitespace(*it)) {
		newprev = ' ';
		// Skip multiple whitespace.
		do {
		    ++it;
		} while (it != end && is_whitespace(*it));
		// Don't generate a group unless the terms are only separated
		// by whitespace.
		if (it != end && is_wordchar(*it)) {
		    if (old_mode == IN_GROUP || old_mode == IN_GROUP2) {
			mode = IN_GROUP2;
		    } else {
			mode = IN_GROUP;
		    }
		}
	    }
	}
    }
done:
    if (!state.error) {
	// Implicitly close any unclosed quotes.
	if (mode == IN_QUOTES || mode == IN_PREFIXED_QUOTES)
	    Parse(&parser, QUOTE, NULL, &state);

	// Implicitly close all unclosed brackets.
	while (prefix_stack.size() > 1) {
	    Parse(&parser, KET, NULL, &state);
	    prefix_stack.pop_back();
	}
	Parse(&parser, 0, NULL, &state);
    }

    errmsg = state.error;
    return state.query;
}

#line 3588 "queryparser/queryparser_internal.cc"
