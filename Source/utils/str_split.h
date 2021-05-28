#pragma once

#include <iterator>
#include <type_traits>

#include "utils/stdcompat/string_view.hpp"

namespace devilution {

class SplitByCharIterator {
public:
	using iterator_category = std::forward_iterator_tag;
	using value_type = std::string_view;
	using reference = std::add_lvalue_reference<value_type>::type;
	using pointer = std::add_pointer<value_type>::type;

	static SplitByCharIterator begin(std::string_view text, char splitBy) // NOLINT(readability-identifier-naming)
	{
		return SplitByCharIterator(splitBy, text, text.substr(0, text.find(splitBy))); // NOLINT(readability-identifier-naming)
	}

	static SplitByCharIterator end(std::string_view text, char splitBy) // NOLINT(readability-identifier-naming)
	{
		return SplitByCharIterator(splitBy, text, text.substr(text.size()));
	}

	std::string_view operator*() const
	{
		return slice_;
	}

	const std::string_view *operator->() const
	{
		return &slice_;
	}

	SplitByCharIterator &operator++()
	{
		slice_ = text_.substr(slice_.data() - text_.data() + slice_.size());
		if (!slice_.empty())
			slice_.remove_prefix(1); // skip the splitBy char
		slice_ = slice_.substr(0, slice_.find(split_by_));
		return *this;
	}

	SplitByCharIterator operator++(int) const
	{
		auto copy = *this;
		++copy;
		return copy;
	}

	bool operator==(const SplitByCharIterator &rhs) const
	{
		return slice_.data() == rhs.slice_.data();
	}

	bool operator!=(const SplitByCharIterator &rhs) const
	{
		return !(*this == rhs);
	}

private:
	SplitByCharIterator(char splitBy, std::string_view text, std::string_view slice)
	    : split_by_(splitBy)
	    , text_(text)
	    , slice_(slice)
	{
	}

	const char split_by_;
	const std::string_view text_;
	std::string_view slice_;
};

class SplitByChar {
public:
	explicit SplitByChar(std::string_view text, char splitBy)
	    : text_(text)
	    , split_by_(splitBy)
	{
	}

	[[nodiscard]] SplitByCharIterator begin() const // NOLINT(readability-identifier-naming)
	{
		return SplitByCharIterator::begin(text_, split_by_);
	}

	[[nodiscard]] SplitByCharIterator end() const // NOLINT(readability-identifier-naming)
	{
		return SplitByCharIterator::end(text_, split_by_);
	}

private:
	const std::string_view text_;
	const char split_by_;
};

} // namespace devilution
