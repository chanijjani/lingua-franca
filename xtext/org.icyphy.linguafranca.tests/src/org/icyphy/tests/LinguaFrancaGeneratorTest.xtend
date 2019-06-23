/*
 * Tests for the Lingua Franca code generator.
 */
package org.icyphy.tests

import com.google.inject.Inject
import java.io.BufferedReader
import java.io.IOException
import java.io.InputStream
import java.io.InputStreamReader
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.Paths
import java.util.LinkedList
import org.eclipse.emf.common.util.URI
import org.eclipse.xtext.generator.GeneratorContext
import org.eclipse.xtext.generator.InMemoryFileSystemAccess
import org.eclipse.xtext.testing.InjectWith
import org.eclipse.xtext.testing.XtextRunner
import org.eclipse.xtext.testing.extensions.InjectionExtension
import org.eclipse.xtext.testing.util.ParseHelper
import org.icyphy.generator.LinguaFrancaGenerator
import org.icyphy.linguaFranca.Model
import org.junit.Test
import org.junit.jupiter.api.Assertions
import org.junit.jupiter.api.^extension.ExtendWith
import org.junit.runner.RunWith

@RunWith(XtextRunner)
@ExtendWith(InjectionExtension)
@InjectWith(LinguaFrancaInjectorProvider)
class LinguaFrancaGeneratorTest {
	@Inject
	ParseHelper<Model> parseHelper
	
	@Inject
	LinguaFrancaGenerator generator
	
	@Test
	def void checkCTestModels() {
		var target = "C"
		var testFiles = readTestFiles(target)
		Assertions.assertNotNull(testFiles, "Couldn't find testFiles.txt file for target: " + target)
		
		var errors = new LinkedList<String>()
		var testCount = 0
     	// Write the generated code to a temporary directory.
    	var directory = Files.createTempDirectory("linguafranca")
    	// The following causes the files to be deleted as soon as the test is done.
    	// Probably don't want that while debugging.
        directory.toFile.deleteOnExit
    	println("Writing code to temporary directory: " + directory)
		for (file: testFiles) {
			testCount++
			compileAndRun(target, file, directory, errors)
		}
		var message = '''Errors: «errors.length» out of «testCount» tests:
* «errors.join("\n* ")»'''
		if (!errors.isEmpty) {
			println("FAILURE:\n" + message)
		}
		Assertions.assertTrue(errors.isEmpty, message)
		println('''SUCCESS. Number of compile and run tests: «testCount»''')
	}
	
	/** Compile the specified test file for the specified target
	 *  and append any encountered errors to the specified list of errors.
	 *  @param target The target.
	 *  @param file The Lingua Franca file name.
	 *  @param directory The directory into which to write temporary files.
	 *  @param errors A list to which to append errors.
	 */
	private def compileAndRun(
		String target, String file, Path directory, LinkedList<String> errors
	) {
        var code = readTestFile(target, file)
        if (code === null) {
        	errors.add("Couldn't find test file: " + file + " for target: " + target)
        	return
        }
        // Check that the file parses.
        println("*** Parsing test file: " + file)
        val parsed = parseHelper.parse(code)
        if (parsed === null) {
        	errors.add('''Parser returned null on file «file».''')
        	return
        }
        val parseErrors = parsed.eResource.errors
        if (!parseErrors.isEmpty) {
        	errors.add('''Parse errors in «file»:
*** «parseErrors.join("\n*** ")»''')
        	return
        }
        
        // Check that code is generated.
        // First, give the resource a file name (for resolving imports, etc.)
        println("Generating code for test file: " + file)
		// Need an absolute path.
		// NOTE: Although Eclipse has no trouble reading from the file specified this way
		// see readTestFile(), it has trouble figuring out what the URI is. I have
		// tried many permutations, and it appears that the following is the only way to make
		// this URI "hierarchical" (whatever that means) and absolute.
		// For imports to work, it has to be both.
		var url = this.class.getResource("/test/src/" + target + "/" + file)
        parsed.eResource.setURI(URI.createFileURI(url.getPath()))
        
        // Create an in-memory filesystem for the result.
        var fsa = new InMemoryFileSystemAccess()
        
        // Generate the code.
        // FIXME: What is the third argument ("context", not documented anywhere).
        generator.doGenerate(parsed.eResource, fsa, new GeneratorContext())
        // Retrieve the generated file.
        var allFiles = fsa.getAllFiles();
   		// Construct the C filename.
   		var cFile = file.substring(0, file.length - 2) + "c"
        // Check that a .c file was generated.
        // Bizarrely, the file name is prefixed with DEFAULT_OUTPUT.
        // This appears to not be changeable...
        var bizarreFilename = "DEFAULT_OUTPUT" + cFile
        if (allFiles.get(bizarreFilename) === null) {
        	errors.add('''File not generated by the code generator: «bizarreFilename»''')
        	return
        }
    	// Write generated files to the temporary directory.
    	for (generatedFile: allFiles.keySet) {
    		// For some inexplicable reason, xtext's InMemoryFileSystemAccess
    		// prefixes all the file names with "DEFAULT_OUTPUT". Remove that junk.
    		var cleanFilename = generatedFile
    		if (generatedFile.startsWith("DEFAULT_OUTPUT")) {
    			cleanFilename = generatedFile.substring(14)
    		}
    		// Files.createFile() blocks forever if the file already exists!!!
    		// Hence, we need to delete it first.
    		var targetFilename = Paths.get(directory.toString, cleanFilename)
    		Files.deleteIfExists(targetFilename)
    		var destinationCodeFile = Files.createFile(Paths.get(directory.toString, cleanFilename))

    		// Delete the files as soon as the test is done.
    		// Probably don't want that while debugging.
    		destinationCodeFile.toFile.deleteOnExit
    		
    		// Read the generated code and write it to the temporary directory.
   			// Second argument is not documented anywhere in xtext.
   			// It is an "output configuration name", whatever the hell that is.
   			var sourceCode = fsa.readTextFile(generatedFile, "")
    		var sourceCodeAsList = new LinkedList<CharSequence>()
   			sourceCodeAsList.add(sourceCode)
   			Files.write(destinationCodeFile, sourceCodeAsList)
       	}
       	
       	// Determine the compile and run commands.
       	// Start with a default, but if there is a "compile" or "run"
       	// parameter to the Target directive, then use those commands.
   		// Construct the output filename.
   		var outputFile = file.substring(0, file.length - 3)
       	var compileCommand = newArrayList()
       	// By default, limit tests to 10 seconds.
       	var runCommand = newArrayList("./" + outputFile, "-stop", "10", "secs")
       	var runCommandOverridden = false;
       	var threads = ""
   		if (parsed.target.parameters !== null) {
   			for (parameter: parsed.target.parameters.assignments) {
   				if (parameter.name.equals("compile")) {
   					// Strip off enclosing quotation marks and split at spaces.
   					val command = parameter.value.substring(1, parameter.value.length - 1).split(' ')
   					compileCommand.clear
   					compileCommand.addAll(command)
   				} else if (parameter.name.equals("run")) {
    				// Strip off enclosing quotation marks and split at spaces.
   					val command = parameter.value.substring(1, parameter.value.length - 1).split(' ')
   					runCommand.clear
   					runCommand.addAll(command)
   					runCommandOverridden = true
   				} else if (parameter.name.equals("threads")) {
   					threads = parameter.value
				}
   			}
   		}
   		if (!runCommandOverridden && !threads.equals("")) {
   			runCommand.add("-threads")
   			runCommand.add(threads)
   		}
   		if (compileCommand.isEmpty()) {
   			if (threads.equals("")) {
   				// Non-threaded version.
   				compileCommand.addAll("cc", "pqueue.c", "reactor.c", cFile, "-o", outputFile)
   			} else {
   				// Threaded version.
   				compileCommand.addAll("cc", "pqueue.c", "reactor_threaded.c", cFile, "-o", outputFile)
   			}
		}
       	
   		// Invoke the compiler on the generated code.
   		println("Compiling with command: " + compileCommand.join(" "))
		var builder = new ProcessBuilder(compileCommand);
		builder.directory(directory.toFile)
		var process = builder.start()
		var stdout = readStream(process.getInputStream())
		var stderr = readStream(process.getErrorStream())
		if (stdout.length() > 0) {
			println("--- Standard output:")
			println(stdout)
		}
		if (stderr.length() > 0) {
			errors.add(stderr.toString)
			println("ERRORS")
			println("--- Standard error:")
			println(stderr)
		} else {
			println("SUCCESS")
			
			// Run the generated code.
   			println("Running with command: " + runCommand.join(" "))
			builder.command(runCommand)
			process = builder.start()
			stdout = readStream(process.getInputStream())
			stderr = readStream(process.getErrorStream())
			if (stdout.length() > 0) {
				println("--- Standard output:")
				println(stdout)
				println("--- End standard output.")
			}
			if (process.exitValue !== 0 || stderr.length() > 0) {
				errors.add("ERROR running: " + runCommand.join(" ")
					+ "\nExecution returned with error code: " + process.exitValue
					+ "\n"
					+ stderr.toString)
			} else if (process.exitValue === 0) {
				println("SUCCESS")
			}
		}
	}
	
	/** Read the specified input stream until an end of file is encountered
	 *  and return the result as a StringBuilder.
	 *  @param stream The stream to read.
	 *  @return The result as a string.
	 */
	private def readStream(InputStream stream) {
		var reader = new BufferedReader(new InputStreamReader(stream))
		var result = new StringBuilder();
		var line = "";
		while ( (line = reader.readLine()) !== null) {
   			result.append(line);
   			result.append(System.getProperty("line.separator"));
		}
		stream.close()
		reader.close()
		result
	}
	
	/** Read the "testFiles.txt" file in the test directory for the specified
	 *  target language and return a list of the filenames for tests in that
	 *  directory.
	 *  @param target The target name.
	 *  @return A list of test files, or null if the testFiles.txt file was not found.
	 */
	private def readTestFiles(String target) {
		var inputStream = this.class.getResourceAsStream("/test/src/" + target + "/testFiles.txt")
		if (inputStream === null) {
			return null
		}
		try {
 			var result = new LinkedList<String>()
			// The following reads a file relative to the classpath.
			// The file needs to be in the src directory.
    		var reader = new BufferedReader(new InputStreamReader(inputStream))
        	var line = ""
        	while ((line = reader.readLine()) !== null) {
            	result.add(line);
        	}
			return result
        } finally {
        	inputStream.close
        }
	}
	
	/** Read a test file for the specified target and return its contents.
	 *  @param target The target name.
	 *  @param filename The file name.
	 *  @return The contents of the file as a String or null if the file cannot be opened.
	 */
	private def readTestFile(String target, String filename) throws IOException {
		var inputStream = this.class.getResourceAsStream("/test/src/" + target + "/" + filename)
		if (inputStream === null) {
			return null
		}
		try {
    		var resultStringBuilder = new StringBuilder()
			// The following reads a file relative to the classpath.
			// The file needs to be in the src directory.
    		var reader = new BufferedReader(new InputStreamReader(inputStream))
        	var line = ""
        	while ((line = reader.readLine()) !== null) {
            	resultStringBuilder.append(line).append("\n");
        	}
			return resultStringBuilder.toString();
        } finally {
        	inputStream.close
        }
	}
}
