#include <exception>
#include <utility>
#include "jumpman.h"

namespace imagine {

void Jumpman::rethrow_exception()
{
	if (m_jump_active)
		std::terminate();

	if (!m_exception) {
		m_default_throw(m_default_throw_arg);
		std::terminate();
	}

	std::exception_ptr ex = nullptr;
	std::swap(ex, m_exception);
	std::rethrow_exception(ex);
}

void Jumpman::execute_jump()
{
	if (!m_jump_active)
		std::terminate();

	std::longjmp(m_setjmp, 1);
}

void Jumpman::store_exception()
{
	if (!m_jump_active)
		std::terminate();

	m_exception = std::current_exception();
}

} // namespace imagine
