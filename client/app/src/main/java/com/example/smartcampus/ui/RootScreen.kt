package com.example.smartcampus.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.viewmodel.compose.viewModel
import com.example.smartcampus.SmartCampusApp
import com.example.smartcampus.ui.auth.AuthViewModel
import com.example.smartcampus.ui.auth.LoginScreen
import com.example.smartcampus.ui.auth.RegisterScreen
import com.example.smartcampus.ui.home.HomeScreen
import com.example.smartcampus.ui.home.HomeViewModel

/**
 * 顶层路由：未登录 → 登录/注册；已登录 → 主界面。
 */
@Composable
fun RootScreen() {
    val app = LocalContext.current.applicationContext as SmartCampusApp
    val container = app.container

    val authViewModel: AuthViewModel = viewModel(factory = AuthViewModel.factory(container))
    val homeViewModel: HomeViewModel = viewModel(factory = HomeViewModel.factory(container))

    val session by container.authRepository.session.collectAsState(initial = null)

    if (session == null) {
        AuthScreen(authViewModel)
    } else {
        HomeScreen(session = session!!, viewModel = homeViewModel)
    }
}

@Composable
private fun AuthScreen(viewModel: AuthViewModel) {
    var showRegister by rememberSaveable { mutableStateOf(false) }
    if (showRegister) {
        RegisterScreen(viewModel, onBackToLogin = { showRegister = false })
    } else {
        LoginScreen(viewModel, onGoRegister = { showRegister = true })
    }
}
