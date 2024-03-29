<?php

/**
 * URL callback that is invoked by git to create a code pull task.
 */
function devshop_pull_callback($project_hosting_context, $hash) {

  module_load_include('module', 'hosting_git');
  module_load_include('inc', 'hosting_git_pull', 'hosting_git_pull.webhook');

  // Load the project node & list of allowed IPs
  $project_node = hosting_context_load("project_{$project_hosting_context}");
  $project = $project_node->project;
  $deploy_settings = $project_node->project->settings->deploy;

  // Make sure we got the project.
  if (!$project_node) {
    $message = "Project $project not found.";
  }
  // Make sure the security code is valid
  else if (_devshop_pull_hash_create($project_node) != $hash) {
    $message = "Security code $hash is not valid!";
    $status = DEVSHOP_PULL_STATUS_INVALID_CODE;
  }
  // Make sure the project has pull callback enabled
  if (empty($deploy_settings['webhooks'])) {
    $message = "Project not configured to use webhook deployment.";
  }
  // Make sure the client's IP address is on the list
  elseif (!devshop_pull_webhook_ip_access()) {
    $message = ip_address() . " is not authorized to invoke a webhook request.";
    $status = DEVSHOP_PULL_STATUS_ACCESS_DENIED;
  }
  // All checks pass! Server is allowed to trigger tasks!
  else {
    $status = DEVSHOP_PULL_STATUS_OK;

    // Check headers for GitHub Integration
    $headers = getallheaders();
    if ((isset($headers['X-GitHub-Event']) || isset($headers['X-Github-Event'])) && function_exists('devshop_github_webhook')) {
      $message = devshop_github_webhook($project_node);
    }
    elseif ($headers['User-Agent'] == 'Bitbucket-Webhooks/2.0' && function_exists('devshop_bitbucket_webhook')) {
      $message = devshop_bitbucket_webhook($project_node);
    }
    else {
      $message = devshop_pull_default_webhook($project_node);
    }
  }

  // Save the project node with last pull info.
  $deploy_settings['last_webhook'] = REQUEST_TIME;
  $deploy_settings['last_webhook_status'] = $status;
  $deploy_settings['last_webhook_ip'] = ip_address();

  $project_node->project->settings->deploy = $deploy_settings;

  node_save($project_node);

  // Output a message, no matter what.
  watchdog('devshop_pull', $message, array(), WATCHDOG_INFO);

  // @TODO Print an appropriate header.
  print $message;

  // Save a variable to help when using the settings page.
  variable_set('devshop_pull_last_ip', ip_address());
  variable_set('devshop_pull_last_status', $status);
}

/**
 * Default action to take on webhook init.
 */
function devshop_pull_default_webhook($project_node) {
  $project = $project_node->project;

  // Check if body corresponds to a JSON object.
  // This is for GitLab Payloads.
  // @TODO: We should move this to it's own module. There's more GitLab integration
  // that could be done.
  $headers = getallheaders();
  if ($headers['Content-Type'] == 'application/json') {
    $input = file_get_contents('php://input');
    $data = json_decode($input);
    if (json_last_error() == JSON_ERROR_NONE){
      // Attempt to get branch from JSON object
      if( isset($data->ref) ) {
        // Attempt to get branch/tag being referred
        $git_ref = strtr($data->ref, array('refs/tags/' => '', 'refs/heads/' => ''));
      }
    }
  }

  foreach ($project->environments as $environment) {

    // If "pull disabled" is set, or if the git ref isn't for this environment, don't trigger a deploy.
    if (empty($environment->hosting_settings['deployment']['continuous']) || (isset($git_ref) && $git_ref != $environment->git_ref)) {
      continue;
    }

    $environments_to_pull[] = $environment->name;

    // Default args to the environments deploy settings.
    $args = $environment->settings->deploy;
    $args['git_ref'] = $environment->git_ref;

    // Ensure that environment site exists before trying to deploy it.
    if (isset($environment->site) && $node = node_load($environment->site)) {
      hosting_add_task($environment->site, 'deploy', $args);
    }
  }

  return "Commit notification received! Running 'Deploy' on '$project->name' environments: " . implode(', ', $environments_to_pull);
}

/**
 * Create the full URL that is displayed in the project node view
 * and given to the GitHub WebHook to invoke a pull after a commit.
 */
function _devshop_pull_callback_url($node) {
  return url(DEVSHOP_PULL_CALLBACK_URL
    . '/' . $node->title
    . '/' . _devshop_pull_hash_create($node),
    array('absolute' => TRUE));
}

/**
 * Create a security hash code based on the platform node
 */
function _devshop_pull_hash_create($node) {
  return md5($node->title . $node->nid);
}

if (!function_exists('getallheaders')) {
  function getallheaders() {
    $headers = '';
    foreach ($_SERVER as $name => $value){
      if (substr($name, 0, 5) == 'HTTP_') {
        $headers[str_replace(' ', '-', ucwords(strtolower(str_replace('_', ' ', substr($name, 5)))))] = $value;
      }
    }
    return $headers;
  }
}
