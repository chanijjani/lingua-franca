/*************
 * Copyright (c) 2021, TU Dresden.

 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:

 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.

 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.

 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ***************/

package org.lflang.generator.cpp

import org.lflang.lf.Reactor
import org.lflang.toText

/**
 * A C++ code generator that produces a C++ class representing a single reactor
 */
class CppReactorGenerator(private val reactor: Reactor, private val fileConfig: CppFileConfig) {

    /** Comment to be inserted at the top of generated files */
    private val fileComment = fileComment(reactor.eResource())

    /** The header file that declares `reactor` */
    private val headerFile = fileConfig.getReactorHeaderPath(reactor).toUnixString()

    /** The header file that contains the public file-level preamble of the file containing `reactor` */
    private val preambleHeaderFile = fileConfig.getPreambleHeaderPath(reactor.eResource()).toUnixString()

    private val state = CppStateGenerator(reactor)
    private val instances = CppInstanceGenerator(reactor, fileConfig)
    private val timers = CppTimerGenerator(reactor)
    private val actions = CppActionGenerator(reactor)
    private val reactions = CppReactionGenerator(reactor)
    private val ports = CppPortGenerator(reactor)
    private val constructor = CppConstructorGenerator(reactor, state, instances, timers, actions)
    private val assemble = CppAssembleMethodGenerator(reactor)

    private fun publicPreamble() =
        reactor.preambles.filter { it.isPublic }
            .joinToString(separator = "\n", prefix = "// public preamble\n") { it.code.toText() }

    private fun privatePreamble() =
        reactor.preambles.filter { it.isPrivate }
            .joinToString(separator = "\n", prefix = "// private preamble\n") { it.code.toText() }

    /** Generate a C++ header file declaring the given reactor. */
    fun generateHeader() = with(prependOperator) {
        """
        ${" |"..fileComment}
            | 
            |#pragma once
            |
            |#include "reactor-cpp/reactor-cpp.hh"
            |
            |#include "$preambleHeaderFile"
            |
        ${" |  "..instances.generateIncludes()}
            |
        ${" |  "..publicPreamble()}
            |
            |// TODO «IF r.isGeneric»«r.templateLine»«ENDIF»
            |class ${reactor.name} : public reactor::Reactor {
            | private:
            |  // TODO «r.declareParameters»
        ${" |  "..state.generateDeclarations()}
        ${" |  "..instances.generateDeclarations()}
        ${" |  "..timers.generateDeclarations()}
        ${" |  "..actions.generateDeclarations()}
        ${" |  "..reactions.generateDeclarations()}
        ${" |  "..reactions.generateBodyDeclarations()}
            |  // TODO «r.declareDeadlineHandlers»
            | public:
        ${" |  "..ports.generateDeclarations()}
        ${" |  "..constructor.generateDeclaration()}
            |
            |  void assemble() override;
            |};
            |/* TODO
            |«IF r.isGeneric»
            |
            |#include "«r.headerImplFile.toUnixString»"
            |«ENDIF»*/
        """.trimMargin()
    }

    /** Generate a C++ source file implementing the given reactor. */
    fun generateSource() = with(prependOperator) {
        """
        ${" |"..fileComment}
            |
            |${if (!reactor.isGeneric) """#include "$headerFile"""" else ""}
            |#include "lfutil.hh"
            |
            |using namespace std::chrono_literals;
            |using namespace reactor::operators;
            |
        ${" |  "..privatePreamble()}
            |
        ${" |"..constructor.generateDefinition()}
            |
        ${" |"..assemble.generateDefinition()}
            |
        ${" |"..reactions.generateBodyDefinitions()}
            |// TODO «r.implementReactionDeadlineHandlers»
        """.trimMargin()
    }
}

