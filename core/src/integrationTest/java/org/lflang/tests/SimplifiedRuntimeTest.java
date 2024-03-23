package org.lflang.tests;

import org.junit.jupiter.api.Assumptions;
import org.junit.jupiter.api.Test;
import org.lflang.target.Target;
import org.lflang.tests.TestRegistry.TestCategory;

/**
 * A collection of JUnit tests to perform on a given set of targets.
 *
 * @author Marten Lohstroh
 * @author Chanhee Lee
 */
public abstract class SimplifiedRuntimeTest extends TestBase {

  /**
   * Construct a test instance that runs tests for a single target.
   *
   * @param target The target to run tests for.
   */
  protected SimplifiedRuntimeTest(Target target) {
    super(target);
  }

  /** Whether to enable {@link #runFederatedTests()}. */
  protected boolean supportsFederatedExecution() {
    return false;
  }

  @Test
  public void runFederatedTestsWithRustRti() {
    Assumptions.assumeTrue(supportsFederatedExecution(), Message.NO_FEDERATION_SUPPORT);
    runTestsForTargetsWithRustRti(
        Message.DESC_FEDERATED_WITH_RUST_RTI,
        TestCategory.FEDERATED::equals,
        Transformers::noChanges,
        Configurators::noChanges,
        TestLevel.EXECUTION,
        false);
  }
}
