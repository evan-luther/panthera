#include "ld.hpp"
#include "Options.h"
#include "parsers/lto_file.h"
#include "passes/bitcode_bundle.h"

namespace lto {

static bool
hasBitcodeMagic(const uint8_t *fileContent, uint64_t fileLength)
{
	return fileLength >= 4 &&
	    fileContent[0] == 0xDE &&
	    fileContent[1] == 0xC0 &&
	    fileContent[2] == 0x17 &&
	    fileContent[3] == 0x0B;
}

const char *
version()
{
	return "libLTO unavailable";
}

bool
libLTOisLoaded()
{
	return false;
}

const char *
archName(const uint8_t *, uint64_t)
{
	return nullptr;
}

bool
isObjectFile(const uint8_t *fileContent, uint64_t fileLength, cpu_type_t,
    cpu_subtype_t)
{
	return hasBitcodeMagic(fileContent, fileLength);
}

ld::relocatable::File *
parse(const uint8_t *fileContent, uint64_t fileLength, const char *, time_t,
    ld::File::Ordinal, cpu_type_t, cpu_subtype_t, bool, bool)
{
	if (!hasBitcodeMagic(fileContent, fileLength))
		return nullptr;
	throw "LLVM bitcode input is not supported in this Panthera ld64 build";
}

bool
optimize(const std::vector<const ld::Atom *> &, ld::Internal &,
    const OptimizeOptions &, ld::File::AtomHandler &,
    std::vector<const ld::Atom *> &, std::vector<const char *> &)
{
	return false;
}

} // namespace lto

namespace ld {
namespace passes {
namespace bitcode_bundle {

void
doPass(const Options &opts, ld::Internal &)
{
	if (opts.bundleBitcode())
		throw "-bitcode_bundle is not supported in this Panthera ld64 build";
}

} // namespace bitcode_bundle
} // namespace passes
} // namespace ld
