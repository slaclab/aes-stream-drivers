.. _github-pages-setup:

Enable GitHub Pages (One-Time Setup)
=====================================

After the first tag push triggers the ``docs.yml`` workflow and creates the
``gh-pages`` branch, you must activate GitHub Pages in the repository settings.
This is a one-time manual step that cannot be automated.

Prerequisites
-------------

* The ``docs.yml`` workflow must have run at least once (triggered by pushing a
  tag). This creates the ``gh-pages`` branch with the built HTML.
* You must have admin access to the repository on GitHub.

Steps
-----

1. Navigate to the repository on GitHub:
   ``https://github.com/slaclab/aes-stream-drivers``

2. Click **Settings** (top navigation bar).

3. In the left sidebar, click **Pages** (under "Code and automation").

4. Under **Source**, select **Deploy from a branch**.

5. Under **Branch**, select ``gh-pages`` from the dropdown.

6. Set the folder to ``/ (root)``.

7. Click **Save**.

GitHub will begin serving the site within a few minutes. The docs will be
available at:

.. code-block:: text

   https://slaclab.github.io/aes-stream-drivers

Subsequent tag pushes will automatically update the site without any further
manual steps.

Verification
------------

After saving the Pages settings, verify the site is live:

.. code-block:: bash

   curl -s -o /dev/null -w "%{http_code}" https://slaclab.github.io/aes-stream-drivers

A ``200`` response confirms the site is serving correctly. If you see ``404``,
wait a few minutes and retry — GitHub Pages propagation can take up to 10 minutes
after the initial activation.

.. note::

   The ``gh-pages`` branch is a deployment artifact managed by the CI workflow.
   Do not commit directly to it or add branch protection rules to it.
